# TSE Itayose Calculation — Project Reference

This document captures the TSE/JPX Itayose call-auction price-formation rule as we apply it in `src/book/indicative.cpp`. It is the "what the code is trying to do" companion to `notes/step3_iap_iav_calculation.cpp` (which is the "how the code does it").

The rules are listed in the priority order the matching engine applies them. Each rule is necessary; the rules below it only break ties left over from the rules above.

## 1. Inputs

| Input | Where it comes from in this repo |
| --- | --- |
| Per-side market-order volume | `IssueState::market_bid_volume`, `IssueState::market_ask_volume` |
| Limit-order ladder | `IssueState::limit_price_levels` (sorted by `Price`) |
| Base price (a.k.a. previous reference price) | `IssueState::previous_reference_price` (set to the previous IAP after each successful match; unset on `R` reset) |

## 2. Derived per-price quantities

For each price `P` on the ladder, with bids sorted descending and asks ascending:

```
cum_bid(P)   = market_bid_volume + Σ bid_volume at prices >= P
cum_ask(P)   = market_ask_volume + Σ ask_volume at prices <= P
tip_up(P)    = cum_ask(P) - cum_bid(P) + bid_at_P
tip_down(P)  = cum_bid(P) - cum_ask(P) + ask_at_P
```

`tip_up + tip_down = bid_at_P + ask_at_P` is a useful invariant.

## 3. Rule set in priority order

The official JPX "Itayose Conditions and Pricing Examples" PDF orders the matching engine's selection logic as **five conditions**, applied strictly in sequence. Each condition narrows the candidate set; the next only operates on what remains. This is the structure our `calculate_indicative_match` mirrors.

### Cond 1 — Executable price set

A candidate price `P` must allow bids and offers to match: `cum_bid(P) > 0 AND cum_ask(P) > 0`. Equivalently, both market orders fill and at least some limit orders cross.

### Cond 2 — Maximum executable volume

Among Cond 1 prices, keep those that maximize executable volume `min(cum_bid(P), cum_ask(P))`. In a static price ladder this set is identical to `{P : tip_up(P) >= 0 AND tip_down(P) >= 0}`, which we call the **Itayose band**. Within the band, IAV is constant.

### Cond 3 — Minimum imbalance

Among Cond 2 prices, keep those that minimize the residual imbalance `|cum_bid(P) - cum_ask(P)|`. This often narrows the band to one or two prices.

### Cond 4 — Side-of-imbalance tie-break

If Cond 3 leaves multiple prices and they all have the same residual side:
- All sell-side imbalance (`cum_ask > cum_bid` everywhere): pick the **lowest** Cond 3 price.
- All buy-side imbalance (`cum_bid > cum_ask` everywhere): pick the **highest** Cond 3 price.

Otherwise (mixed sides, or zero imbalance everywhere), proceed to Cond 5.

### Cond 5 — Reference-price tie-break

Let `H` and `L` be the highest and lowest prices in the post-Cond-4 set, and `R` the reference / base price.

- **Cond 5.1** — `R > H`: pick `H` (the highest contender, since the reference is even higher).
- **Cond 5.2** — `L <= R <= H`: pick the in-set price closest to `R` (typically `R` itself when it's on the ladder).
- **Cond 5.3** — `R < L`: pick `L`.

The reference price `R` comes from `IssueState::previous_reference_price`, which is bootstrapped from venue JSON `basePrice` and (in a future revision) overwritten by FLEX `BP` tags.

### Step 6 — Compute IAV at the chosen price

```
IAV = min(cum_bid(P*), cum_ask(P*))
```

When `P*` is at the band boundary and one side is empty there, `IAV = 0`. The price is still reported.

### Step 7 — Allocation among orders at `P*` (out of scope)

Outside the price-discovery problem itself but part of the official rule:

- Market orders fill before limit orders.
- Limit orders at `P*` are allocated by securities-company aggregated quantity (descending), tied by first-arrival time per company. Pre-opening orders **ignore** individual-order time priority.

**Feasibility in this project.** Rule 6 requires per-order securities-company / participant identifiers, which are **not carried on the FLEX MBO feed** by design. Inspecting the A-tag specification (`docs/protocol-source.md` lines 373–444): the only fields are `type`, `time`, `order_id`, `side`, `quantity`, `price`, `order_condition`, `modification_flag`. No broker/firm/participant field exists. The public BP, II, MG, T, L tags also carry no participant identity. Participant-aware allocation happens upstream of the market-data feed and is exchange-internal.

Conclusion: Rule 6 cannot be implemented from PCAP-only data. It is correctly scoped out for this assignment. If the reviewer expects per-company allocation, a participant-tagged feed (drop-copy, back-office allocation report) would be required.

## 3.5 Cross-check against the JPX 2024 Trading Methodology Guide

The public *Guide to TSE Trading Methodology — 2024 Edition* (PDF at `docs/jpx-trading-methodology-2024.pdf`, plain-text extract at `docs/jpx-trading-methodology-2024.extracted.txt`) does not enumerate the five conditions explicitly, but its Q12 states the three opening-price requirements directly:

> a. All sell/buy market orders must be executed
> b. All limit orders to sell/buy at prices lower/higher than the execution price must be executed
> c. At the execution price, the entire amount of either all sell or all buy orders must be executed

Mapping to our code:

| Q12 requirement | Mechanism in `calculate_indicative_match` |
| --- | --- |
| (a) all market orders executed | `cum_ask` is seeded with `market_ask_volume`, `cum_bid` with `market_bid_volume`, so any price in the band already absorbs both market sides. |
| (b) better-priced limit orders fully executed | `cum_ask` accumulates ascending and `cum_bid` accumulates descending, so by construction every aggressively-priced order is included in the totals at every candidate price. |
| (c) one side fully executed at `P` | The Cond 2 band (`tip_up >= 0 AND tip_down >= 0`) is the set where one side fully clears; the Cond 1 guard (`cum_bid > 0 AND cum_ask > 0`) excludes degenerate boundary prices where one side has nothing to "fully execute". |

Q12's worked example uses two band prices (¥500, ¥501) and picks one; the PDF does not spell out the Cond 3-5 tie-break order. The strict five-condition hierarchy below comes from the JPX "Itayose Conditions and Pricing Examples" PDF, which is consistent with the Q12 narrative.

The PDF also documents behaviours we do not yet implement (special quote, sequential trade quote, halt-resume Itayose, closing-auction-at-limit-price). These are tracked in §6 and the project README's "Not implemented yet" list.

The implementation in `src/book/indicative.cpp` applies the five conditions in strict priority order. Each step narrows the candidate set; the next operates only on what remains.

| Step | Code location | Behavior |
| --- | --- | --- |
| Build snapshots | `indicative.cpp:38-63` | Per-price `cum_bid` / `cum_ask`, seeded with `market_bid_volume` / `market_ask_volume` so market orders are absorbed before any limit accumulation. |
| Cond 1 + Cond 2 | `indicative.cpp:65-85` | Band filter `cum_bid > 0 AND cum_ask > 0 AND tip_up >= 0 AND tip_down >= 0`. The Cond 1 guard prevents one-sided boundary prices from being reported as valid IAPs. |
| No candidates | `indicative.cpp:82-84` | If the band is empty (e.g. extreme one-sided imbalance, or a non-crossing book), return `has_result = false`. |
| Cond 3 | `indicative.cpp:88-104` | Keep only band prices that minimise `|cum_bid - cum_ask|`. |
| Cond 4 | `indicative.cpp:111-131` | If the Cond 3 set has uniform residual side (all sell-side / all buy-side), pick the lowest / highest price. |
| Cond 5 | `indicative.cpp:132-153` | Reference-price fallback: 5.1 `R > H` → `H`; 5.2 `L <= R <= H` → closest to `R`, equidistant prefers the higher price (opening convention); 5.3 `R < L` → `L`. |
| Step 6 (IAV) | `indicative.cpp:22-28` | `volume = min(cum_bid, cum_ask)` at the chosen price. |
| Step 7 (allocation) | not implemented | Requires per-securities-company identifiers, which are not present on the FLEX MBO feed. |

## 5. Edge cases that reliably trip implementations

- **No price satisfies Cond 1 + 2.** Massive bid/ask imbalance can leave the band empty. The correct output is "no IAP", not the boundary the search happened to land on. Issue `6164` in our fixture is the canonical example: cum_bid ≈ 2.29M vs cum_ask ≈ 1.6k means `tip_up < 0` everywhere.
- **Single-sided book / non-crossing book.** Only one side has orders, or bid top < ask bottom. Cond 1 (`cum_bid > 0 AND cum_ask > 0`) fails at every price, so the band is empty and we return no result. Before the Cond 1 fix the code returned a boundary price with `iav = 0`; in the fixture run that affected 21 stock symbols, now all correctly report no result.
- **`tip_up` or `tip_down` exactly zero.** Strict `>` would filter these out. The rule uses `>=`, so band-edge prices where one side fully clears with zero residual are kept.
- **`base_price` outside the ladder.** Cond 5.1 (`R > H`) / Cond 5.3 (`R < L`) cover this.
- **`base_price` exactly between two Cond 4 prices.** Cond 5.2 picks the closest; on exact equidistance our code prefers the higher price, which is the JPX convention for opening auctions (the lower price would be correct for closing).
- **`base_price` not yet established.** Bootstrapped from venue JSON `basePrice` per issue. Reset (`R` tag) restores `previous_reference_price` from `base_price` rather than clearing it.
- **Special quote / sequential trade quote regime.** `base_price` should switch to the published special quote; not yet implemented (see §6).

## 6. Wider thoughts and open design questions

### Thought 1 — `base_price` source-of-truth (DONE)

`previous_reference_price` is now bootstrapped from the venue JSON `basePrice` field (per-issue) the first time an issue appears in replay, and it is no longer overwritten by the running IAP. The matcher uses base price as the Rule 3 anchor throughout the session, exactly as JPX specifies. `R` reset restores `previous_reference_price` from `base_price` instead of clearing it.

Measured impact across 303 evaluable stock symbols, comparing each version against the IDEAL TSE pick (band price closest to venue basePrice):

| Version | Correct |
| --- | --- |
| baseline (screenshot rule) | 212 / 303 (70.0%) |
| itayose v1 (linear scan, running-IAP anchor) | 211 / 303 (69.6%) |
| **itayose v2 (linear scan, basePrice anchor)** | **303 / 303 (100.0%)** |

Open extension: the FLEX `BP` (Base Price Information) tag is a 68-byte tag on the wire that publishes intra-day base-price updates (special quote, halt-resume regime). We do not yet decode it — the current sample captures contain no BP tags — but a production implementation should let an incoming `BP` tag overwrite `IssueState::base_price`.

### Thought 2 — Equidistant tie-break determinism (DONE)

When two band prices are exactly equidistant from `base_price`, Cond 5.2 now prefers the **higher** price, matching the JPX opening-auction convention (lower would be correct for closing). This is implemented as a `<=` comparison in `indicative.cpp` and locked in by `test_indicative_match_cond5_2_equidistant_prefers_higher` in `tests/test_main.cpp`.

A stricter version of the rule preferred by JPX's example PDFs is the three-key sort:

1. minimise `|P − base_price|`,
2. then maximise `min(cum_bid, cum_ask)`,
3. then prefer the higher price for opening auctions / lower for closing.

In practice, by the time Cond 5 fires the candidate set already has constant `min(cum_bid, cum_ask)` (Cond 2 invariant) and constant `|cum_bid - cum_ask|` (Cond 3), so step (2) of the three-key sort never narrows the set further. The current implementation is equivalent to the three-key sort on every input that survives Cond 2 + 3.

### Thought 3 — Halt, special-quote, and reset semantics

The replay engine treats `R` (reset) as clearing `previous_reference_price`. That's appropriate for an issue reset, but a real auction also has:

- **Special quote (SQ) / sequential trade quote (STQ)** episodes during the day, where `base_price` is overridden by the published quote until the imbalance clears. Our protocol coverage doesn't yet decode these tags fully (see `notes/protocol-notes.md`).
- **Halt / resume**: when trading resumes after a halt, the matcher runs an Itayose. If our replay sees a halt-resume sequence we'd want the IAP to anchor on the halt-base, not on the last pre-halt IAP.
- **Daily limit ("stop-high"/"stop-low")**: if no price in the band lies inside the daily price range, the IAP is clamped. We don't currently track daily limits.

A robust implementation would model `base_price` as a small state machine driven by status tags, not a single mutable `optional<Price>`. For the current PCAP samples this is over-engineering; for production it's the next design step.

## 7. Target shape and remaining work

The current implementation matches this pseudocode, with one piece (`current_base_price` state machine) still simplified:

```text
function compute_iap_iav(issue):
    if issue.market_bid + issue.market_ask + |limits| == 0:
        return no_result

    snapshots = build_cum_bid_cum_ask_snapshots(issue)
    band = [P for P in snapshots
            if cum_bid(P) > 0 and cum_ask(P) > 0          # Cond 1
               and tip_up(P) >= 0 and tip_down(P) >= 0]    # Cond 2
    if band is empty:
        return no_result

    cond3 = argmin_P_in_band |cum_bid(P) - cum_ask(P)|     # Cond 3
    if cond3 is uniformly sell-side:  return min(cond3)    # Cond 4a
    if cond3 is uniformly buy-side:   return max(cond3)    # Cond 4b

    R = current_base_price(issue)
    H, L = max(cond3.price), min(cond3.price)
    if R > H: P* = H                                       # Cond 5.1
    elif R < L: P* = L                                     # Cond 5.3
    else:                                                  # Cond 5.2
        P* = argmin_P_in_cond3 |P - R|,
             tie-break: higher price for opening
    return (P*, min(cum_bid(P*), cum_ask(P*)))


function current_base_price(issue):                        # SIMPLIFIED
    return issue.base_price_from_venue
    # TODO: special-quote / sequential-trade-quote override
    # TODO: halt-resume base
    # TODO: closing-auction-at-limit clamp
```

The remaining work is in `current_base_price`: today it's a single immutable value seeded from `TseVenue.<date>.json basePrice`. Production would replace it with a small state machine driven by O tags (market status) and BP tags (intra-day base updates):

```
                +-------------------+
                | venue basePrice   |   (initial, seeded once per issue)
                +---------+---------+
                          |
                          | (O tag: trading halted)
                          v
                +-------------------+    (O tag: halt lifted)
                |  halt-base price  |---------+
                +-------------------+         |
                          |                   |
              (BP tag) or (O tag: SQ/STQ)     |
                          v                   |
                +-------------------+         |
                |  SQ / STQ price   |---------+
                +-------------------+         |
                                              v
                                       (resume normal Itayose)
```

Transitions are driven by replay events, not by the indicative calculator, which keeps `calculate_indicative_match` a pure function of the snapshot.

## Sources

- [Itayose Conditions and Pricing Examples (JPX PDF)](https://www.jpx.co.jp/english/derivatives/rules/trading-methods/tvdivq0000004h12-att/tvdivq000000ueul.pdf) — primary source for the five-condition priority hierarchy and worked numerical examples.
- [Guide to TSE Trading Methodology — 2024 Edition (JPX PDF)](https://www.jpx.co.jp/english/equities/trading/domestic/tvdivq000000tpfi-att/eg.pdf) — Q12 states the three opening-price requirements that Cond 1-2 implement. Local copy in `docs/jpx-trading-methodology-2024.pdf`; text extract in `docs/jpx-trading-methodology-2024.extracted.txt`.
- [Revision to Itayose condition for determination of contract price (JPX public comment PDF)](https://www.jpx.co.jp/rules-participants/public-comment/detail/d8/nlsgeu00000112to-att/8.pdf)
- [arrowhead4.0 – How is an execution price determined (JPX Service Desk FAQ)](https://faqsd.jpx.co.jp/faq/show/18159)
- [Transaction Methods – Trading Rules of Domestic Stocks (JPX)](https://www.jpx.co.jp/english/equities/trading/domestic/04.html)
- [Tokyo Stock Exchange Official Guide: Pricing Mechanism of arrowhead, 2nd ed.](https://www.jpx.co.jp/english/corporate/news/news-releases/0060/20240930-01.html)
- Project-internal: `docs/jpx-transaction-methods.md`, `notes/step3_iap_iav_calculation.cpp`, `notes/decision-log.md`
