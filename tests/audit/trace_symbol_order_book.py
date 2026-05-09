#!/usr/bin/env python3
"""Trace order-by-order book changes for one symbol from decoded step1 data.

This is audit/test tooling only. It does not import or call production C++ code.
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass, field
from decimal import Decimal
from pathlib import Path


MARKET_PRICE = "MARKET"


@dataclass
class Order:
    side: str
    quantity: int
    price: str
    order_condition: int


@dataclass
class PriceLevel:
    bid_volume: int = 0
    ask_volume: int = 0


@dataclass
class Book:
    live_orders: dict[int, Order] = field(default_factory=dict)
    levels: dict[str, PriceLevel] = field(default_factory=lambda: defaultdict(PriceLevel))
    market_bid_volume: int = 0
    market_ask_volume: int = 0
    last_sequence: int = 0
    last_update: int = 0


def normalized_price(value: str) -> str:
    if value == MARKET_PRICE:
        return MARKET_PRICE
    if value == "":
        return value
    return f"{Decimal(value):.4f}"


def is_opening_eligible(order: Order) -> bool:
    return order.order_condition in (0, 2)


def side_volume(level: PriceLevel, side: str) -> int:
    return level.bid_volume if side == "B" else level.ask_volume


def add_to_book(book: Book, order: Order) -> int:
    if not is_opening_eligible(order) or order.quantity == 0:
        return 0
    if order.price == MARKET_PRICE:
        if order.side == "B":
            before = book.market_bid_volume
            book.market_bid_volume += order.quantity
            return book.market_bid_volume - before
        if order.side == "S":
            before = book.market_ask_volume
            book.market_ask_volume += order.quantity
            return book.market_ask_volume - before
        return 0

    level = book.levels[order.price]
    before = side_volume(level, order.side)
    if order.side == "B":
        level.bid_volume += order.quantity
    elif order.side == "S":
        level.ask_volume += order.quantity
    return side_volume(level, order.side) - before


def remove_from_book(book: Book, order: Order, quantity: int) -> int:
    if not is_opening_eligible(order) or quantity == 0:
        return 0
    amount = min(quantity, order.quantity)
    if order.price == MARKET_PRICE:
        if order.side == "B":
            before = book.market_bid_volume
            book.market_bid_volume = max(0, book.market_bid_volume - amount)
            return book.market_bid_volume - before
        if order.side == "S":
            before = book.market_ask_volume
            book.market_ask_volume = max(0, book.market_ask_volume - amount)
            return book.market_ask_volume - before
        return 0

    level = book.levels.get(order.price)
    if level is None:
        return 0
    before = side_volume(level, order.side)
    if order.side == "B":
        level.bid_volume = max(0, level.bid_volume - amount)
    elif order.side == "S":
        level.ask_volume = max(0, level.ask_volume - amount)
    after = side_volume(level, order.side)
    if level.bid_volume == 0 and level.ask_volume == 0:
        del book.levels[order.price]
    return after - before


def level_volume(book: Book, price: str, side: str) -> int:
    if price == MARKET_PRICE:
        return book.market_bid_volume if side == "B" else book.market_ask_volume
    level = book.levels.get(price)
    if level is None:
        return 0
    return side_volume(level, side)


def book_totals(book: Book) -> tuple[int, int]:
    bid_total = book.market_bid_volume
    ask_total = book.market_ask_volume
    for level in book.levels.values():
        bid_total += level.bid_volume
        ask_total += level.ask_volume
    return bid_total, ask_total


def format_order(order: Order | None) -> str:
    if order is None:
        return ""
    return f"{order.side} {order.quantity}@{order.price} cond={order.order_condition}"


def load_audit_issue(audit_csv: Path, symbol: str) -> dict[str, object]:
    issue: dict[str, object] = {
        "levels": {},
        "market_bid_volume": 0,
        "market_ask_volume": 0,
        "live_orders": 0,
        "last_sequence": 0,
        "last_update": 0,
    }
    levels: dict[str, PriceLevel] = {}
    with audit_csv.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["symbol"] != symbol:
                continue
            issue["market_bid_volume"] = int(row["market_bid_volume"])
            issue["market_ask_volume"] = int(row["market_ask_volume"])
            issue["live_orders"] = int(row["live_orders"])
            issue["last_sequence"] = int(row["last_sequence"])
            issue["last_update"] = int(row["last_update"])
            bid_volume = int(row["bid_volume"])
            ask_volume = int(row["ask_volume"])
            price = normalized_price(row["price"])
            if price != "0.0000" or bid_volume != 0 or ask_volume != 0:
                levels[price] = PriceLevel(bid_volume=bid_volume, ask_volume=ask_volume)
    issue["levels"] = levels
    return issue


def compare_final(book: Book, audit_issue: dict[str, object]) -> list[str]:
    mismatches: list[str] = []
    scalar_checks = [
        ("market_bid_volume", book.market_bid_volume, int(audit_issue["market_bid_volume"])),
        ("market_ask_volume", book.market_ask_volume, int(audit_issue["market_ask_volume"])),
        ("live_orders", len(book.live_orders), int(audit_issue["live_orders"])),
        ("last_sequence", book.last_sequence, int(audit_issue["last_sequence"])),
        ("last_update", book.last_update, int(audit_issue["last_update"])),
    ]
    for name, actual, expected in scalar_checks:
        if actual != expected:
            mismatches.append(f"{name}: trace={actual} audit={expected}")

    audit_levels = audit_issue["levels"]
    assert isinstance(audit_levels, dict)
    for price in sorted(set(book.levels) | set(audit_levels), key=Decimal):
        trace_level = book.levels.get(price, PriceLevel())
        audit_level = audit_levels.get(price, PriceLevel())
        if trace_level.bid_volume != audit_level.bid_volume:
            mismatches.append(
                f"{price} bid_volume: trace={trace_level.bid_volume} audit={audit_level.bid_volume}"
            )
        if trace_level.ask_volume != audit_level.ask_volume:
            mismatches.append(
                f"{price} ask_volume: trace={trace_level.ask_volume} audit={audit_level.ask_volume}"
            )
    return mismatches


def write_event_trace(
    step1_csv: Path,
    symbol: str,
    trace_csv: Path,
) -> tuple[Book, dict[str, int]]:
    trace_csv.parent.mkdir(parents=True, exist_ok=True)
    book = Book()
    stats = {
        "input_rows": 0,
        "book_events": 0,
        "ignored_non_book_rows": 0,
        "unknown_deletes": 0,
        "unknown_executes": 0,
    }

    with step1_csv.open(newline="", encoding="utf-8") as input_handle, trace_csv.open(
        "w", newline="", encoding="utf-8"
    ) as output_handle:
        fieldnames = [
            "event_index",
            "capture_jst",
            "file",
            "record_index",
            "sequence_number",
            "update_number",
            "tag_type",
            "order_id",
            "action",
            "side",
            "price",
            "order_condition",
            "quantity",
            "book_delta",
            "level_before",
            "level_after",
            "live_orders_after",
            "market_bid_volume_after",
            "market_ask_volume_after",
            "total_bid_volume_after",
            "total_ask_volume_after",
            "note",
        ]
        writer = csv.DictWriter(output_handle, fieldnames=fieldnames)
        writer.writeheader()

        for row in csv.DictReader(input_handle):
            if row["issue_code"] != symbol:
                continue
            stats["input_rows"] += 1
            tag_type = row["tag_type"]
            book.last_sequence = int(row["sequence_number"])
            book.last_update = int(row["update_number"])

            if tag_type not in ("A", "D", "E", "C", "R"):
                stats["ignored_non_book_rows"] += 1
                continue

            action = ""
            side = ""
            price = ""
            order_condition = ""
            quantity = ""
            book_delta = 0
            level_before = 0
            level_after = 0
            note = ""
            order_id = row["order_id"]

            if tag_type == "A":
                order = Order(
                    side=row["side"],
                    quantity=int(row["quantity"]),
                    price=normalized_price(row["price"]),
                    order_condition=int(row["order_condition"]),
                )
                order_id_int = int(order_id)
                old_order = book.live_orders.get(order_id_int)
                if old_order is not None:
                    remove_from_book(book, old_order, old_order.quantity)
                    note = f"replaced old order {format_order(old_order)}"
                side = order.side
                price = order.price
                order_condition = str(order.order_condition)
                quantity = str(order.quantity)
                level_before = level_volume(book, price, side)
                book.live_orders[order_id_int] = order
                book_delta = add_to_book(book, order)
                level_after = level_volume(book, price, side)
                action = "add"
                if not is_opening_eligible(order):
                    note = "tracked live order but excluded from opening book"
            elif tag_type == "D":
                order_id_int = int(order_id)
                old_order = book.live_orders.get(order_id_int)
                if old_order is None:
                    stats["unknown_deletes"] += 1
                    note = "delete ignored: unknown order_id"
                    action = "delete_ignored"
                else:
                    side = old_order.side
                    price = old_order.price
                    order_condition = str(old_order.order_condition)
                    quantity = str(old_order.quantity)
                    level_before = level_volume(book, price, side)
                    book_delta = remove_from_book(book, old_order, old_order.quantity)
                    level_after = level_volume(book, price, side)
                    del book.live_orders[order_id_int]
                    action = "delete"
            elif tag_type in ("E", "C"):
                order_id_int = int(order_id)
                old_order = book.live_orders.get(order_id_int)
                if old_order is None:
                    stats["unknown_executes"] += 1
                    note = "execute ignored: unknown order_id"
                    action = "execute_ignored"
                else:
                    executed_quantity = int(row["executed_quantity"])
                    side = old_order.side
                    price = old_order.price
                    order_condition = str(old_order.order_condition)
                    quantity = str(executed_quantity)
                    level_before = level_volume(book, price, side)
                    book_delta = remove_from_book(book, old_order, executed_quantity)
                    level_after = level_volume(book, price, side)
                    if executed_quantity >= old_order.quantity:
                        del book.live_orders[order_id_int]
                    else:
                        old_order.quantity -= executed_quantity
                    action = "execute"
            elif tag_type == "R":
                book = Book()
                action = "reset"

            if action:
                stats["book_events"] += 1
                total_bid, total_ask = book_totals(book)
                writer.writerow(
                    {
                        "event_index": stats["book_events"],
                        "capture_jst": row["capture_jst"],
                        "file": row["file"],
                        "record_index": row["record_index"],
                        "sequence_number": row["sequence_number"],
                        "update_number": row["update_number"],
                        "tag_type": tag_type,
                        "order_id": order_id,
                        "action": action,
                        "side": side,
                        "price": price,
                        "order_condition": order_condition,
                        "quantity": quantity,
                        "book_delta": book_delta,
                        "level_before": level_before,
                        "level_after": level_after,
                        "live_orders_after": len(book.live_orders),
                        "market_bid_volume_after": book.market_bid_volume,
                        "market_ask_volume_after": book.market_ask_volume,
                        "total_bid_volume_after": total_bid,
                        "total_ask_volume_after": total_ask,
                        "note": note,
                    }
                )

    return book, stats


def write_final_book(book: Book, final_book_csv: Path) -> None:
    final_book_csv.parent.mkdir(parents=True, exist_ok=True)
    running_ask = book.market_ask_volume
    rows = []
    for price in sorted(book.levels, key=Decimal):
        level = book.levels[price]
        running_ask += level.ask_volume
        rows.append(
            {
                "price": price,
                "bid_volume": level.bid_volume,
                "ask_volume": level.ask_volume,
                "cum_bid": 0,
                "cum_ask": running_ask,
            }
        )

    running_bid = book.market_bid_volume
    for row in reversed(rows):
        running_bid += int(row["bid_volume"])
        row["cum_bid"] = running_bid

    with final_book_csv.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = ["price", "bid_volume", "ask_volume", "cum_bid", "cum_ask"]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_summary(
    summary_path: Path,
    symbol: str,
    step1_csv: Path,
    audit_csv: Path,
    trace_csv: Path,
    final_book_csv: Path,
    book: Book,
    stats: dict[str, int],
    mismatches: list[str],
) -> None:
    total_bid, total_ask = book_totals(book)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with summary_path.open("w", encoding="utf-8") as handle:
        handle.write(f"# Symbol Order Book Trace: {symbol}\n\n")
        handle.write(f"step1_csv={step1_csv}\n")
        handle.write(f"step2_audit_csv={audit_csv}\n")
        handle.write(f"event_trace_csv={trace_csv}\n")
        handle.write(f"final_book_csv={final_book_csv}\n\n")
        for key, value in stats.items():
            handle.write(f"{key}={value}\n")
        handle.write(f"live_orders={len(book.live_orders)}\n")
        handle.write(f"price_levels={len(book.levels)}\n")
        handle.write(f"market_bid_volume={book.market_bid_volume}\n")
        handle.write(f"market_ask_volume={book.market_ask_volume}\n")
        handle.write(f"total_bid_volume={total_bid}\n")
        handle.write(f"total_ask_volume={total_ask}\n")
        handle.write(f"last_sequence={book.last_sequence}\n")
        handle.write(f"last_update={book.last_update}\n")
        handle.write("\n")
        if mismatches:
            handle.write("status=FAIL\n")
            handle.write("mismatches:\n")
            for mismatch in mismatches:
                handle.write(f"  {mismatch}\n")
        else:
            handle.write("status=PASS\n")
            handle.write("interpretation=The order-by-order trace final book matches the step2 audit for this symbol.\n")


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[2]
    results_dir = repo_root / "build" / "results"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--symbol", default="1382", help="Issue code to trace.")
    parser.add_argument(
        "--step1-csv",
        type=Path,
        default=results_dir / "step1_decoded_messages.csv",
        help="Decoded step1 CSV generated by fixture test.",
    )
    parser.add_argument(
        "--step2-audit-csv",
        type=Path,
        default=results_dir / "iap_iav_audit.csv",
        help="Step2 audit CSV generated by fixture test.",
    )
    parser.add_argument("--out-dir", type=Path, default=results_dir, help="Output directory.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    symbol = args.symbol
    trace_csv = args.out_dir / f"symbol_{symbol}_order_book_trace.csv"
    final_book_csv = args.out_dir / f"symbol_{symbol}_final_book_from_trace.csv"
    summary_path = args.out_dir / f"symbol_{symbol}_order_book_trace_summary.txt"

    book, stats = write_event_trace(args.step1_csv, symbol, trace_csv)
    write_final_book(book, final_book_csv)
    audit_issue = load_audit_issue(args.step2_audit_csv, symbol)
    mismatches = compare_final(book, audit_issue)
    write_summary(
        summary_path,
        symbol,
        args.step1_csv,
        args.step2_audit_csv,
        trace_csv,
        final_book_csv,
        book,
        stats,
        mismatches,
    )

    print(f"Symbol: {symbol}")
    print(f"Book events: {stats['book_events']}")
    print(f"Live orders: {len(book.live_orders)}")
    print(f"Price levels: {len(book.levels)}")
    print(f"Mismatches: {len(mismatches)}")
    print(f"Trace CSV written to {trace_csv}")
    print(f"Final book CSV written to {final_book_csv}")
    print(f"Summary written to {summary_path}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
