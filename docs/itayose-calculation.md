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

## 4. How the rules map to `calculate_indicative_match`

| Rule | Code |
| --- | --- |
| 1 | early `return {}` when ladder + market volumes are all empty |
| 2 | `if (tip_up < 0 \|\| tip_down < 0) continue;` filter |
| 3 | `if (best == nullptr \|\| distance < best_distance)` selection |
| 4 | currently first-encountered (ascending price) — equidistant ties go to lower price; should be enhanced (see §6) |
| 5 | `make_result()` → `volume = min(cum_bid, cum_ask)` |
| 6 | not implemented — out of assignment scope |

## 5. Edge cases that reliably trip implementations

- **No price satisfies Rule 2.** Massive bid/ask imbalance can leave the band empty. The correct output is "no IAP", not the boundary the search happened to land on. Issue `6164` in our fixture is the canonical example: cum_bid ≈ 2.29M vs cum_ask ≈ 1.6k means `tip_up < 0` everywhere.
- **`tip_up` or `tip_down` exactly zero.** Strict `>` filters these out. The rule is `>=`.
- **Single-sided book.** Only one side has orders. The band collapses to boundary prices where one side has zero volume; IAP exists, IAV is zero.
- **`base_price` outside the ladder.** Closest-distance still has a unique answer (the nearest end of the band).
- **`base_price` not yet established.** Before the first successful IAP, `previous_reference_price` is unset and our code falls back to `0`. For deep books that's effectively "minimize price" — wrong as a rule but harmless because the band typically straddles the previous close, which is set before pre-open ticks arrive in real systems.
- **Special quote / sequential trade quote regime.** `base_price` switches to the published special quote. The math is unchanged.

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

### Thought 2 — Equidistant tie-break determinism

Right now, when two band prices are exactly equidistant from `base_price`, our linear scan picks the first encountered (lowest price by std::map iteration order). The fixture data shows two such ties (issues `6229`, `6848`). JPX's published examples prefer larger executable volume; if even that ties, "buy-side preferred" picks the higher price. A robust tie-break should:

1. minimize `|P − base_price|`,
2. then maximize `min(cum_bid, cum_ask)`,
3. then prefer the higher price for opening auctions / lower for closing auctions.

This is a 5-line addition to the linear scan. Recommended.

### Thought 3 — Halt, special-quote, and reset semantics

The replay engine treats `R` (reset) as clearing `previous_reference_price`. That's appropriate for an issue reset, but a real auction also has:

- **Special quote (SQ) / sequential trade quote (STQ)** episodes during the day, where `base_price` is overridden by the published quote until the imbalance clears. Our protocol coverage doesn't yet decode these tags fully (see `notes/protocol-notes.md`).
- **Halt / resume**: when trading resumes after a halt, the matcher runs an Itayose. If our replay sees a halt-resume sequence we'd want the IAP to anchor on the halt-base, not on the last pre-halt IAP.
- **Daily limit ("stop-high"/"stop-low")**: if no price in the band lies inside the daily price range, the IAP is clamped. We don't currently track daily limits.

A robust implementation would model `base_price` as a small state machine driven by status tags, not a single mutable `optional<Price>`. For the current PCAP samples this is over-engineering; for production it's the next design step.

## 7. Robust algorithm (proposed)

Pulling Thoughts 1–3 together, here is the target shape:

```text
function compute_iap_iav(issue):
    if issue.market_bid + issue.market_ask + |limits| == 0:
        return no_result

    snapshots = build_cum_bid_cum_ask_snapshots(issue)
    band = [P for P in snapshots if tip_up(P) >= 0 and tip_down(P) >= 0]
    if band is empty:
        return no_result

    base = current_base_price(issue)            # see state machine below

    candidates = sort band by:
        primary   = |P - base|                  # ascending  (Rule 3)
        secondary = -min(cum_bid(P), cum_ask(P))# descending (Rule 4a)
        tertiary  = -P if opening else +P       # (Rule 4b)

    P* = candidates[0]
    return (P*, min(cum_bid(P*), cum_ask(P*)))


function current_base_price(issue):
    if issue is in special_quote_state:
        return issue.special_quote_price
    if issue.has_running_iap:
        return issue.last_iap
    return issue.prev_close_price_from_venue    # bootstrap from venue JSON
```

State machine for `base_price`:

```
                +-----------------+
                | venue prev_close |   (initial)
                +-------+---------+
                        |
                first IAP
                        v
                +-----------------+
                |  running IAP    |
                +-------+---------+
                        |
            special-quote tag    R reset
                  |   |     |   |
                  v   v     v   v
            +--------+   +-------------+
            |  SQ/STQ |   | venue prev_close |
            +--------+   +-------------+
```

Each state knows what to return for `current_base_price()`. Transitions are driven by replay events, not by the indicative calculator. That keeps the indicative function pure.

This is a strict superset of the current code. Rules 1, 2, 3, 5 are already implemented; Rule 4 is partial (deterministic but not volume-aware); Rule 6 is out of scope; the base-price state machine is the largest piece of new work.

## Sources

- [Itayose Conditions and Pricing Examples (JPX PDF)](https://www.jpx.co.jp/english/derivatives/rules/trading-methods/tvdivq0000004h12-att/tvdivq000000ueul.pdf)
- [Revision to Itayose condition for determination of contract price (JPX public comment PDF)](https://www.jpx.co.jp/rules-participants/public-comment/detail/d8/nlsgeu00000112to-att/8.pdf)
- [arrowhead4.0 – How is an execution price determined (JPX Service Desk FAQ)](https://faqsd.jpx.co.jp/faq/show/18159)
- [Transaction Methods – Trading Rules of Domestic Stocks (JPX)](https://www.jpx.co.jp/english/equities/trading/domestic/04.html)
- [Tokyo Stock Exchange Official Guide: Pricing Mechanism of arrowhead, 2nd ed.](https://www.jpx.co.jp/english/corporate/news/news-releases/0060/20240930-01.html)
- Project-internal: `docs/jpx-transaction-methods.md`, `notes/step3_iap_iav_calculation.cpp`, `notes/decision-log.md`
