# Assignment Output

Final deliverable for the take-home assignment: the last calculated indicative opening-auction price and volume per stock, derived from the supplied PCAP captures.

## Files

- `iap_iav_20241105.csv` — `symbol,iap,iav` rows for every stock observed in the 2024-11-05 captures (`securityType` 01-04 in `TseVenue.20241105.json`).
- `audit/step2_order_book_audit.csv` — supplementary per-price-level audit for every replayed issue: `cum_bid`, `cum_ask`, `tip_up`, `tip_down`, and a `selected=yes` row marking the chosen IAP/IAV. Reviewers can use this to verify the Itayose result for any symbol without re-running the tests.
- `audit/step1_fixture_report.txt` — human-readable summary of the capture inputs: record counts, FLEX tag-type counts, per-capture issue-code → venue-name mappings, and source endpoints.

`audit/` is a committed snapshot of the corresponding files that the fixture test regenerates under `build/results/` on every run. The step1 per-tag CSV (one row per decoded FLEX tag, ~107 MB for this capture) is intentionally excluded from the committed snapshot — run `ctest` to regenerate it locally.

## Breakdown of the rows in `iap_iav_20241105.csv`

- **304 total rows** — one row per stock.
- **272 rows** have a valid IAP with `iav > 0`.
- **32 rows** report no result (`iap=0.0000, iav=0`):
  - 10 rows where replay never accumulated any opening-eligible orders (e.g. `1452`, `152A`, `154A`, `158A`).
  - 22 rows where the book has orders on both sides but no price satisfies Cond 1 (`cum_bid(P) > 0 AND cum_ask(P) > 0`) — non-crossing books and extreme one-sided imbalances (e.g. `1770`, `2481`, `3600`, `3954`, `6164`).

Both no-result classes correctly emit `iap=0.0000, iav=0` under the JPX Itayose rule.

## How it was generated

```bash
./build/tse_mbo \
  --pcap data/20241105_051.test.pcap.gz \
  --pcap data/20241105_052.test.pcap.gz \
  --venue-json data/TseVenue.20241105.json \
  --csv-out output/iap_iav_20241105.csv \
  --summary-only
```

See the top-level `README.md` for build instructions, the Itayose rule, and details on the full set of audit artifacts (the step1 per-tag CSV is regenerated under `build/results/` by the fixture test).
