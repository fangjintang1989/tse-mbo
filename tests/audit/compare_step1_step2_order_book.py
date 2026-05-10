#!/usr/bin/env python3
"""Compare decoded step1 messages with the final step2 order-book audit.

This is audit/test tooling only. It intentionally replays the decoded step1 CSV
without using the production C++ order-book code, then compares the rebuilt
final book against build/results/step2_order_book_audit.csv.
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from decimal import Decimal
from pathlib import Path
from typing import Iterable


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
class IssueBook:
    live_orders: dict[int, Order] = field(default_factory=dict)
    levels: dict[str, PriceLevel] = field(default_factory=lambda: defaultdict(PriceLevel))
    market_bid_volume: int = 0
    market_ask_volume: int = 0
    last_sequence: int = 0
    last_update: int = 0


@dataclass
class RebuildResult:
    books: dict[str, IssueBook]
    tag_counts: Counter[str]
    issue_codes_by_file: dict[str, set[str]]
    unknown_delete_count: int = 0
    unknown_execute_count: int = 0


@dataclass
class AuditIssue:
    levels: dict[str, PriceLevel] = field(default_factory=dict)
    market_bid_volume: int = 0
    market_ask_volume: int = 0
    live_orders: int = 0
    last_sequence: int = 0
    last_update: int = 0


def is_opening_eligible(order: Order) -> bool:
    return order.order_condition in (0, 2)


def add_order_to_book(book: IssueBook, order: Order) -> None:
    if not is_opening_eligible(order) or order.quantity == 0:
        return
    if order.price == MARKET_PRICE:
        if order.side == "B":
            book.market_bid_volume += order.quantity
        elif order.side == "S":
            book.market_ask_volume += order.quantity
        return
    level = book.levels[order.price]
    if order.side == "B":
        level.bid_volume += order.quantity
    elif order.side == "S":
        level.ask_volume += order.quantity


def remove_order_from_book(book: IssueBook, order: Order, quantity: int) -> None:
    if not is_opening_eligible(order) or quantity == 0:
        return
    amount = min(quantity, order.quantity)
    if amount == 0:
        return
    if order.price == MARKET_PRICE:
        if order.side == "B":
            book.market_bid_volume = max(0, book.market_bid_volume - amount)
        elif order.side == "S":
            book.market_ask_volume = max(0, book.market_ask_volume - amount)
        return
    level = book.levels.get(order.price)
    if level is None:
        return
    if order.side == "B":
        level.bid_volume = max(0, level.bid_volume - amount)
    elif order.side == "S":
        level.ask_volume = max(0, level.ask_volume - amount)
    if level.bid_volume == 0 and level.ask_volume == 0:
        del book.levels[order.price]


def normalized_price(value: str) -> str:
    if value == MARKET_PRICE:
        return MARKET_PRICE
    if value == "":
        return value
    return f"{Decimal(value):.4f}"


def replay_step1(step1_csv: Path) -> RebuildResult:
    books: dict[str, IssueBook] = defaultdict(IssueBook)
    tag_counts: Counter[str] = Counter()
    issue_codes_by_file: dict[str, set[str]] = defaultdict(set)
    unknown_delete_count = 0
    unknown_execute_count = 0

    with step1_csv.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            tag_type = row["tag_type"]
            tag_counts[tag_type] += 1

            issue_code = row["issue_code"]
            if issue_code == "<control>" or issue_code == "":
                if tag_type == "R":
                    books.clear()
                continue

            issue_codes_by_file[row["file"]].add(issue_code)
            book = books[issue_code]
            book.last_sequence = int(row["sequence_number"])
            book.last_update = int(row["update_number"])

            if tag_type == "A":
                order_id = int(row["order_id"])
                order = Order(
                    side=row["side"],
                    quantity=int(row["quantity"]),
                    price=normalized_price(row["price"]),
                    order_condition=int(row["order_condition"]),
                )
                old_order = book.live_orders.get(order_id)
                if old_order is not None:
                    remove_order_from_book(book, old_order, old_order.quantity)
                book.live_orders[order_id] = order
                add_order_to_book(book, order)
            elif tag_type == "D":
                order_id = int(row["order_id"])
                old_order = book.live_orders.get(order_id)
                if old_order is None:
                    unknown_delete_count += 1
                    continue
                remove_order_from_book(book, old_order, old_order.quantity)
                del book.live_orders[order_id]
            elif tag_type in ("E", "C"):
                order_id = int(row["order_id"])
                old_order = book.live_orders.get(order_id)
                if old_order is None:
                    unknown_execute_count += 1
                    continue
                executed_quantity = int(row["executed_quantity"])
                remove_order_from_book(book, old_order, executed_quantity)
                if executed_quantity >= old_order.quantity:
                    del book.live_orders[order_id]
                else:
                    old_order.quantity -= executed_quantity
            elif tag_type == "R":
                books.clear()

    return RebuildResult(
        books=dict(books),
        tag_counts=tag_counts,
        issue_codes_by_file={key: set(value) for key, value in issue_codes_by_file.items()},
        unknown_delete_count=unknown_delete_count,
        unknown_execute_count=unknown_execute_count,
    )


def load_step2_audit(audit_csv: Path) -> dict[str, AuditIssue]:
    issues: dict[str, AuditIssue] = {}
    with audit_csv.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            symbol = row["symbol"]
            issue = issues.setdefault(symbol, AuditIssue())
            issue.market_bid_volume = int(row["market_bid_volume"])
            issue.market_ask_volume = int(row["market_ask_volume"])
            issue.live_orders = int(row["live_orders"])
            issue.last_sequence = int(row["last_sequence"])
            issue.last_update = int(row["last_update"])

            price = normalized_price(row["price"])
            bid_volume = int(row["bid_volume"])
            ask_volume = int(row["ask_volume"])
            if price == "0.0000" and bid_volume == 0 and ask_volume == 0:
                continue
            issue.levels[price] = PriceLevel(
                bid_volume=bid_volume,
                ask_volume=ask_volume,
            )
    return issues


def sorted_prices(prices: Iterable[str]) -> list[str]:
    return sorted(prices, key=lambda price: Decimal(price))


def compare_books(
    rebuilt: dict[str, IssueBook],
    audit: dict[str, AuditIssue],
) -> list[dict[str, str]]:
    mismatches: list[dict[str, str]] = []
    for symbol in sorted(audit):
        rebuilt_issue = rebuilt.get(symbol, IssueBook())
        audit_issue = audit[symbol]

        scalar_checks = [
            ("market_bid_volume", rebuilt_issue.market_bid_volume, audit_issue.market_bid_volume),
            ("market_ask_volume", rebuilt_issue.market_ask_volume, audit_issue.market_ask_volume),
            ("live_orders", len(rebuilt_issue.live_orders), audit_issue.live_orders),
            ("last_sequence", rebuilt_issue.last_sequence, audit_issue.last_sequence),
            ("last_update", rebuilt_issue.last_update, audit_issue.last_update),
        ]
        for field_name, rebuilt_value, audit_value in scalar_checks:
            if rebuilt_value != audit_value:
                mismatches.append(
                    {
                        "symbol": symbol,
                        "field": field_name,
                        "price": "",
                        "rebuilt": str(rebuilt_value),
                        "step2": str(audit_value),
                    }
                )

        prices = set(rebuilt_issue.levels) | set(audit_issue.levels)
        for price in sorted_prices(prices):
            rebuilt_level = rebuilt_issue.levels.get(price, PriceLevel())
            audit_level = audit_issue.levels.get(price, PriceLevel())
            if rebuilt_level.bid_volume != audit_level.bid_volume:
                mismatches.append(
                    {
                        "symbol": symbol,
                        "field": "bid_volume",
                        "price": price,
                        "rebuilt": str(rebuilt_level.bid_volume),
                        "step2": str(audit_level.bid_volume),
                    }
                )
            if rebuilt_level.ask_volume != audit_level.ask_volume:
                mismatches.append(
                    {
                        "symbol": symbol,
                        "field": "ask_volume",
                        "price": price,
                        "rebuilt": str(rebuilt_level.ask_volume),
                        "step2": str(audit_level.ask_volume),
                    }
                )
    return mismatches


def write_mismatches_csv(path: Path, mismatches: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        fieldnames = ["symbol", "field", "price", "rebuilt", "step2"]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(mismatches)


def write_summary(
    path: Path,
    step1_path: Path,
    audit_path: Path,
    rebuilt: RebuildResult,
    audit: dict[str, AuditIssue],
    mismatches: list[dict[str, str]],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    issue_sets = list(rebuilt.issue_codes_by_file.values())
    overlap_count = len(set.intersection(*issue_sets)) if len(issue_sets) >= 2 else 0
    union_count = len(set.union(*issue_sets)) if issue_sets else 0

    with path.open("w", encoding="utf-8") as handle:
        handle.write("# Step1 vs Step2 Order Book Check\n\n")
        handle.write(f"step1_csv={step1_path}\n")
        handle.write(f"step2_audit_csv={audit_path}\n")
        handle.write(f"compared_symbols={len(audit)}\n")
        handle.write(f"rebuilt_symbols={len(rebuilt.books)}\n")
        handle.write(f"mismatch_count={len(mismatches)}\n")
        handle.write(f"unknown_delete_count={rebuilt.unknown_delete_count}\n")
        handle.write(f"unknown_execute_count={rebuilt.unknown_execute_count}\n")
        handle.write("\n")
        handle.write("tag_counts:\n")
        for tag_type, count in sorted(rebuilt.tag_counts.items()):
            handle.write(f"  {tag_type}={count}\n")
        handle.write("\n")
        handle.write("issues_by_file:\n")
        for file_name, issues in sorted(rebuilt.issue_codes_by_file.items()):
            handle.write(f"  {file_name}={len(issues)}\n")
        handle.write(f"  union={union_count}\n")
        handle.write(f"  overlap={overlap_count}\n")
        handle.write("\n")
        if mismatches:
            handle.write("status=FAIL\n")
            handle.write("first_mismatches:\n")
            for mismatch in mismatches[:20]:
                handle.write(
                    "  "
                    f"symbol={mismatch['symbol']} field={mismatch['field']} "
                    f"price={mismatch['price']} rebuilt={mismatch['rebuilt']} "
                    f"step2={mismatch['step2']}\n"
                )
        else:
            handle.write("status=PASS\n")
            handle.write(
                "interpretation=The independently rebuilt opening-eligible final book "
                "matches the step2 audit snapshot for every compared symbol.\n"
            )


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[2]
    results_dir = repo_root / "build" / "results"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--step1-csv",
        type=Path,
        default=results_dir / "step1_decoded_messages.csv",
        help="Decoded step1 CSV generated by tse_mbo_fixture_tests.",
    )
    parser.add_argument(
        "--step2-audit-csv",
        type=Path,
        default=results_dir / "step2_order_book_audit.csv",
        help="Step2 final order-book audit CSV generated by tse_mbo_fixture_tests.",
    )
    parser.add_argument(
        "--summary-out",
        type=Path,
        default=results_dir / "step1_step2_order_book_check.txt",
        help="Summary report output path.",
    )
    parser.add_argument(
        "--mismatches-out",
        type=Path,
        default=results_dir / "step1_step2_order_book_mismatches.csv",
        help="Detailed mismatch CSV output path.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rebuilt = replay_step1(args.step1_csv)
    audit = load_step2_audit(args.step2_audit_csv)
    mismatches = compare_books(rebuilt.books, audit)
    write_mismatches_csv(args.mismatches_out, mismatches)
    write_summary(
        args.summary_out,
        args.step1_csv,
        args.step2_audit_csv,
        rebuilt,
        audit,
        mismatches,
    )

    print(f"Compared symbols: {len(audit)}")
    print(f"Rebuilt symbols: {len(rebuilt.books)}")
    print(f"Mismatches: {len(mismatches)}")
    print(f"Summary written to {args.summary_out}")
    print(f"Mismatch CSV written to {args.mismatches_out}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
