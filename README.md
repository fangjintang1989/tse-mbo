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

- `src/cli/` contains the executable entrypoint.
- `src/app/` contains app orchestration, argument parsing, and CSV export.
- `src/ingest/` contains gzip PCAP reading and Ethernet/IP/UDP decoding.
- `src/flex/` contains FLEX packet header and tag parsing.
- `src/book/` contains order-book replay and indicative calculation.
- `tests/` contains synthetic unit coverage and real-capture fixture regression coverage.
- `docs/` contains the original assignment/protocol files and extracted summaries.
- `notes/` contains working decisions, assumptions, open questions, and the readable IAP/IAV calculation note.

```text
src/
  cli/
  app/
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
- `build/results/step3_iap_iav_fixture_results.csv` with per-stock `symbol,iap,iav` output derived from the sample captures
- `build/results/iap_iav_audit.csv` with final per-price audit rows using `bid_price`, `bid_volume`, `ask_price`, `ask_volume`, `cum_bid`, `cum_ask`, `tip_up`, and `tip_down`; the `selected=yes` row explains the final IAP/IAV for a symbol
- `build/results/step1_step2_order_book_check.txt` and `build/results/step1_step2_order_book_mismatches.csv` from the independent step1-to-step2 replay checker
- `build/results/symbol_1382_order_book_trace.csv` and `build/results/symbol_1382_order_book_trace_summary.txt` from the per-symbol order-by-order trace checker

PCAP summary:

| Scope | Count |
| --- | ---: |
| Unique issue codes seen in PCAPs | 343 |
| Security type `01-04` issue codes | 304 |
| Non-stock issue codes filtered from the stock CSV | 39 |
| Final stock CSV rows with IAP/IAV result | 273 |
| Final stock CSV rows with no IAP/IAV result | 31 |
| Final all-issues CSV rows with IAP/IAV result | 304 |
| Final all-issues CSV rows with no IAP/IAV result | 39 |

The assignment text says stocks are security types `1-4`, so issue codes like `1570` with `securityType=B1` are intentionally excluded from the stock-only CSV even though they are still replayed and traced in the order-book logic.

Filtered examples:

- Step 1 to book trace ignores non-book FLEX rows such as `T`, `O`, and `L` because they do not change the order book state.
- Stock CSV filtering removes non-stock issue codes such as `1570`, `1475`, and `1541` because their venue `securityType` is not `01-04` (`1570` is `B1`).
- Final stock CSV rows can still have no IAP/IAV result even for stock issues; examples are `1452`, `152A`, `154A`, and `1770`, which replay but end with zero executable auction volume in the current data.

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
  --csv-out build/results/iap_iav_results.csv \
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

This repo currently implements all three stages, including rolling IAP/IAV calculation and CSV export in `src/book/indicative.*`.

## Current Status

Implemented:

- gzipped PCAP reading
- classic PCAP record decoding
- Ethernet / IPv4 / UDP decoding
- FLEX packet header parsing
- variable-length tag splitting
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
