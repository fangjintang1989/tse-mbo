# Assignment Output

Final deliverable for the take-home assignment: the last calculated indicative opening-auction price and volume per stock, derived from the supplied PCAP captures.

## Files

- `iap_iav_20241105.csv` — `symbol,iap,iav` rows for every stock observed in the 2024-11-05 captures (`securityType` 01-04 in `TseVenue.20241105.json`).

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
  --pcap ../20241105_051.test.pcap.gz \
  --pcap ../20241105_052.test.pcap.gz \
  --venue-json ../TseVenue.20241105.json \
  --csv-out output/iap_iav_20241105.csv \
  --summary-only
```

See the top-level `README.md` for build instructions, the Itayose rule, and audit CSVs in `build/results/`.
