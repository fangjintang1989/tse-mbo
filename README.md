# tse-mbo

Historical TSE FLEX Full MBO processing assignment in C++.

## Requirements

- CMake `>= 3.20`
- A C++20 compiler
- Zlib development package available to CMake

This repo is currently validated in the following environment:

- OS: Ubuntu 26.04 LTS
- Compiler: `g++ 15.2.0`
- Build system: `CMake 4.2.3`
- Language standard: `C++20`

## Assignment Inputs

- `docs/assignment-original.docx` is the original Word assignment file.
- `docs/protocol-original.docx` is the original Word protocol file.
- `docs/protocol-original.pdf` is the original protocol PDF file.
- `docs/assignment-source.md` contains the extracted Word document text.
- `docs/protocol-source.md` contains the extracted Word protocol text.
- `docs/protocol-summary.md` is the concise protocol summary plus open questions.
- `notes/` keeps the working memory, decisions, and implementation plan for this repo.

The original runtime inputs currently live in the parent workspace:

- `../20241105_051.test.pcap.gz`
- `../20241105_052.test.pcap.gz`
- `../TseVenue.20241105.json`

## Project Layout

- The code path is split as a small pipeline:
  - `src/replay/` loads all PCAPs, stable-sorts capture records by timestamp, and dispatches decoded data
  - `src/ingest/` decodes gzip PCAP, Ethernet, IPv4, and UDP frames
  - `src/flex/` parses FLEX headers/tags and normalizes raw messages at the replay boundary
  - `src/tse/` provides the user-facing engine facade over the book
  - `src/book/` owns order-book replay and IAP/IAV calculation
  - `src/app/` handles CLI arguments and CSV export
  - `src/cli/` is the executable entrypoint
- `tests/` contains synthetic unit coverage plus real-capture fixture regression coverage.
- `docs/` contains the original assignment/protocol files, official source notes, and extracted summaries.
- `notes/` contains working decisions, assumptions, open questions, and the readable IAP/IAV calculation note.

```text
src/
  cli/
  app/
  replay/
  tse/
  ingest/
  flex/
  book/
tests/
docs/
notes/
```

## Build

Configure:

```bash
cmake -S . -B build
```

Build:

```bash
cmake --build build -j
```

## Test

The repo includes a small self-contained test binary for the implemented assignment scope.

Run:

```bash
ctest --test-dir build --output-on-failure
```

Test layout:

- `tse_mbo_unit_tests`: self-contained synthetic coverage for PCAP read/decode, FLEX splitting, and replay basics
- `tse_mbo_fixture_tests`: real-capture regression using the provided sample PCAPs when those files are present next to the repo workspace

Fixture result artifact:

- `build/results/step1_fixture_report.txt` with counts, endpoints, sample datagrams, and per-capture `issue_code -> venue name` mappings
- `build/results/step1_decoded_messages.csv` with one row per decoded FLEX tag from the PCAPs
- `build/results/step2_order_book_audit.csv` with final per-price audit rows using `bid_price`, `bid_volume`, `ask_price`, `ask_volume`, `cum_bid`, `cum_ask`, `tip_up`, and `tip_down`; the `selected=yes` row explains the final IAP/IAV for a symbol
- `build/results/step3_iap_iav_fixture_results.csv` with per-stock `symbol,iap,iav` output derived from the sample captures

Optional audit outputs:

- `build/results/step1_step2_order_book_check.txt` and `build/results/step1_step2_order_book_mismatches.csv` from the independent step1-to-step2 replay checker
- `build/results/symbol_<issue>_order_book_trace.csv` and `build/results/symbol_<issue>_order_book_trace_summary.txt` from the per-symbol order-by-order trace checker

Replay ordering:

- Replay loads all capture records from all supplied PCAPs first, stable-sorts by capture timestamp, then replays into one rolling order book.
- For the current fixture captures, this produces `304` stock rows with `0` IAP/IAV mismatches against the merged replay audit.

PCAP-to-output breakdown:

```text
20241105_051.test.pcap.gz: 171 issue codes
20241105_052.test.pcap.gz: 172 issue codes
Overlap between files: 0 issue codes
        |
        v
PCAP decoded issue codes: 343 total
        |
        v
Order-book replay: all 343 issue codes
        |
        +--> Non-stock issue codes: 39
        |    Replayed for audit, excluded from assignment CSV
        |
        v
Stock issue codes by venue JSON securityType 01-04: 304
        |
        +--> iav > 0: 273 rows
        |
        +--> iav = 0: 31 rows
```

The `iav = 0` rows split into two groups:

- `10` rows with no IAP/IAV result at all because the replayed book never accumulates any opening-eligible orders
- `21` rows with a valid IAP but zero IAV because the selected price is a one-sided boundary and `min(cum_bid, cum_ask)` is `0`

| Stage | Count | What It Means |
| --- | ---: | --- |
| `20241105_051.test.pcap.gz` issue codes | 171 | First multicast-group capture partition. |
| `20241105_052.test.pcap.gz` issue codes | 172 | Second multicast-group capture partition. |
| Overlap between PCAP files | 0 | The files cover different symbols in the same time window. |
| PCAP decoded issue codes | 343 | Unique symbols found across both captures. |
| Replayed issue codes | 343 | Every decoded symbol is applied to order-book state. |
| Non-stock issue codes | 39 | Replayed but excluded from assignment CSV because `securityType` is not `01-04`. |
| Final stock CSV rows | 304 | Assignment output scope after applying the stock filter. |
| Stock rows with `iav > 0` | 273 | A non-zero executable auction volume exists. |
| Stock rows with `iav = 0` | 31 | Symbol is included, but no executable auction volume exists in this capture. |

Both the production CLI and fixture test process every supplied PCAP path. In the normal sample run, `--pcap ../20241105_051.test.pcap.gz --pcap ../20241105_052.test.pcap.gz` means both files are decoded and replayed into the same final order-book state.

Per-capture details:

| PCAP | Time Range JST | Endpoint | Decoded FLEX Tag Rows | Issue Codes | Main Tags |
| --- | --- | --- | ---: | ---: | --- |
| `20241105_051.test.pcap.gz` | `07:10:00.448099196` to `08:59:59.999866426` | `10.17.13.58:51551 -> 224.0.220.51:51551` | 193,618 | 171 | `T=94,790`, `A=86,998`, `D=11,548`, `O=171`, `L=111` |
| `20241105_052.test.pcap.gz` | `07:10:00.448123490` to `08:59:59.999985828` | `10.17.13.68:51552 -> 224.0.220.52:51552` | 236,912 | 172 | `T=115,978`, `A=101,938`, `D=18,713`, `O=172`, `L=111` |

For debug only, an all-issues CSV can be generated without the venue filter. That file has `343` rows: `304` with executable auction volume and `39` with zero executable auction volume.

The assignment text says stocks are security types `1-4`, so issue codes like `1570` with `securityType=B1` are intentionally excluded from the stock-only CSV even though they are still replayed and traced in the order-book logic.

Filtered examples:

- Step 1 to book trace ignores non-book FLEX rows such as `T`, `O`, and `L` because they do not change the order book state.
- Stock CSV filtering removes non-stock issue codes such as `1570`, `1475`, and `1541` because their venue `securityType` is not `01-04` (`1570` is `B1`).
- Final stock CSV rows can still have no IAP/IAV result even for stock issues; examples are `1452`, `152A`, `154A`, and `158A`, which replay but never accumulate any opening-eligible book state.
- Some stock rows do have an IAP but still end with `iav = 0`; examples are `1770`, `2481`, `3600`, and `3954`, where the selected price is a boundary and one side is empty at the match price.

## Run

Summary-only parser/replay run:

```bash
./build/tse_mbo \
  --pcap ../20241105_051.test.pcap.gz \
  --pcap ../20241105_052.test.pcap.gz \
  --venue-json ../TseVenue.20241105.json \
  --summary-only
```

CSV output run:

```bash
./build/tse_mbo \
  --pcap ../20241105_051.test.pcap.gz \
  --pcap ../20241105_052.test.pcap.gz \
  --venue-json ../TseVenue.20241105.json \
  --csv-out build/results/step3_iap_iav_results.csv \
  --summary-only
```

Verbose run with sample issue output:

```bash
./build/tse_mbo \
  --pcap ../20241105_051.test.pcap.gz \
  --pcap ../20241105_052.test.pcap.gz \
  --venue-json ../TseVenue.20241105.json
```

## Assignment Scope

The assignment is being implemented in three stages:

1. read historical packet capture data
2. replay parsed messages into an order book
3. calculate indicative match price and volume

This repo currently implements all three stages, including rolling IAP/IAV calculation and CSV export in `src/book/indicative.*`, with `src/tse/` as the user-facing engine facade.

## Current Status

Implemented:

- gzipped PCAP reading
- classic PCAP record decoding
- Ethernet / IPv4 / UDP decoding
- FLEX packet header parsing
- variable-length tag splitting
- timestamp-merged replay across all supplied PCAPs
- replay handling for `A`, `D`, `E`, `C`, and `R`
- FLEX `Bn` prices decoded from raw fixed-point integers into real decimal prices at the replay boundary
- opening-eligible price-ladder reconstruction, including market-order aggregation
- rolling indicative opening price/volume calculation using the screenshot-derived rule
- venue-filtered CSV export in `symbol,iap,iav` format

Not implemented yet:

- full typed decoding of all protocol tags
- fully verified split-packet transaction handling
- exchange-rule transformations beyond the currently replayed opening-eligible state

## Assumptions And Limits

- Input capture format is gzipped classic PCAP, not PCAP-NG.
- The current PCAP reader supports little-endian classic PCAP files.
- The network decoder currently targets Ethernet + IPv4 + UDP frames.
- A single UDP payload may contain multiple FLEX packets back-to-back; the parser handles this case.
- Order-book replay is implemented for `A`, `D`, `E`, `C`, and `R`, but protocol coverage is not yet complete for every tag type.
- FLEX price fields are decoded once when `A` tags are replayed: the unsigned wire integer uses four decimal places, so raw `17770000` becomes real price `1777.0000`; market-order max price is kept as an internal sentinel and excluded from the limit-price ladder.
- Step 3 currently uses the screenshot-derived IAP/IAV rule captured in `notes/step3_iap_iav_calculation.cpp`.

## Notes

- The structure is intentionally split into a thin CLI app plus reusable modules under `src/` because that reads more professionally for an interview submission than a single flat source directory, without adding unnecessary nesting.
- The current replay engine is an incremental scaffold; synthetic tests are included for the implemented parser and replay paths, but the final submission still needs further protocol-specific validation against more capture scenarios.
