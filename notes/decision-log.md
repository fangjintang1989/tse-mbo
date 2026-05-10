# Decision Log

## 2026-05-08

- Created repo-local `notes/` directory to act as persistent working memory.
- Agreed to break the assignment into 3 steps:
  1. get PCAP data
  2. parse to order book
  3. calculate indicative auction price and volume (IAP/IAV)
- Agreed to defer step 3 until the user provides the exact calculation rule.
- Chose to focus first on step 1 and step 2 in C++.
- Decided to predefine a calculation function boundary before implementing the final logic.
- Chose to keep protocol interpretation and open questions in repo docs instead of relying on hidden assistant memory.

## 2026-05-09

- Added a readable step 3 note so the screenshot transcription can be reviewed with clearer IAP/IAV naming.
- Implemented rolling step 3 IAP/IAV calculation on top of replayed opening-eligible book state.
- Added CSV export for `symbol,iap,iav` plus real-capture fixture output for step 3.
- Decided all downstream order-book, IAP/IAV, fixture, and CSV prices use real decimal prices. Raw FLEX `Bn` prices are converted once at replay decode using four fractional digits.
- Cleaned the repository layout to a single `src/` code root and removed generated build output, the large generated PCAP dump log, and empty legacy scaffolding directories.

## 2026-05-10

- Reviewed the screenshot-derived IAP search against published JPX Itayose rules
  ("Itayose Conditions and Pricing Examples" PDF and the arrowhead pricing FAQ).
  Three departures from the rule:
    - acceptance used strict `tip_up > 0 && tip_down > 0` instead of `>=`,
      so band-edge prices were skipped;
    - the cursor was seeded with `lower_bound`, picking the first price >= ref
      rather than the closest price to the reference;
    - the reversal tie-break compared level volumes instead of distance to the
      previous reference price.
- Replaced the cursor walk with a single linear scan over the snapshots that
  collects every price with `tip_up >= 0 && tip_down >= 0` and picks the one
  with minimum `abs(price - previous_reference_price)`. IAV formula
  (`min(cum_bid, cum_ask)`) and the inclusion of market orders are unchanged.
- Snapshotted the prior step1/step2/step3 CSVs to `build/results/baseline/`
  before running the change. After the rewrite: step1 byte-identical; step2
  audit shows updated `iap`/`iav`/`tip_*` columns reflecting the new selection;
  step3 changed 72 of 305 IAP values, all with the same IAV except for issue
  6164 whose book has no `tip_up >= 0` price at all (massive bid-side
  imbalance) — the baseline 1695 was a boundary fall-through under the old
  walk and is not a TSE-valid opening price.
- Wired venue `basePrice` (TseVenue JSON) as the Itayose anchor. Until now
  `previous_reference_price` was bootstrapped from the first successful IAP
  and overwritten on every subsequent recalc. That conflated three different
  concepts: previous-day base price, running indicative price, and last
  execution price. Per JPX rule, Rule 3 ("closest to base price") uses the
  previous-day close, not the running IAP. New behavior: `IssueState`
  carries an immutable `base_price`, seeded from venue JSON when the issue
  is first observed; `previous_reference_price` is set to `base_price` and
  no longer mutated by `recalculate_issue_state`. `R` reset restores
  `previous_reference_price` from `base_price` instead of clearing it.
- Quantitative impact, measured against "the ideal TSE pick = band price
  closest to venue basePrice" across 303 evaluable stock symbols:
    - old baseline (screenshot rule):           212 correct (70.0%)
    - itayose v1 (fix algo only):               211 correct (69.6%)
    - itayose v2 (fix algo + basePrice anchor): 303 correct (100.0%)
  The v1 step alone barely moved accuracy — the screenshot algorithm and
  the v1 algorithm were each wrong about ~30% of the time, just on
  different symbols. The basePrice anchor closed the remaining gap.
