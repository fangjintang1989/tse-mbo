# tse-mbo

A C++ application that processes historical Tokyo Stock Exchange FLEX Full MBO PCAP captures, replays the orders into an order book, and writes the last calculated indicative opening-auction price and volume (IAP/IAV) per stock to a CSV.

The committed deliverable for the sample PCAPs is [`output/iap_iav_20241105.csv`](output/iap_iav_20241105.csv).

## AI tools used

The assignment asks for disclosure of which AI tools were used.

- **OpenAI Codex (GPT-5-Codex)** wrote the initial implementation: the gzipped PCAP reader, Ethernet/IPv4/UDP and FLEX decoders, the timestamp-merged replay engine, the order-book replay for `A`/`D`/`E`/`C`/`R`, the venue-JSON loader, the CLI, the fixture-test pipeline that produces the per-tag and per-price-level audit CSVs, and the first 5-condition Itayose implementation. All commits in `git log` are authored by `Codex <codex@openai.com>`.
- **Anthropic Claude Opus 4.7** ran a review pass on the completed code and made two correctness fixes (Cond 1 enforcement in `src/book/indicative.cpp` and Cond 5.2 equidistance tie-break), cross-checked the algorithm against the JPX *Guide to TSE Trading Methodology — 2024 Edition* Q12, refreshed the unit tests for the fix, trimmed the project, and rewrote this README.

## Environment

This repo was developed and validated on:

- **OS:** Ubuntu 26.04 LTS (also runs on WSL2 / Ubuntu 24.04 with the same toolchain versions)
- **Compiler:** `g++` 15.2.0
- **Build system:** CMake 4.2.3 (any `>= 3.20` should work)
- **Language standard:** C++20
- **Other:** zlib development headers (Ubuntu: `sudo apt install zlib1g-dev`)

## Sample data

Everything needed to build, run, and validate the project is committed in-repo. No external downloads.

| Path | Purpose |
| --- | --- |
| `data/20241105_051.test.pcap.gz` | Sample FLEX Full MBO capture, session 051 (3.5 MB, gzipped classic PCAP) |
| `data/20241105_052.test.pcap.gz` | Sample FLEX Full MBO capture, session 052 (4.3 MB) |
| `data/TseVenue.20241105.json` | Venue catalog: per-issue `securityType` (stock filter) and `basePrice` (Itayose reference price `R`) |
| `output/iap_iav_20241105.csv` | Committed assignment deliverable, reproduced byte-for-byte by the CLI command below |
| `docs/assignment-original.docx` | Original take-home assignment |
| `docs/protocol-original.{docx,pdf}`, `docs/protocol-source.md` | FLEX Full MBO protocol spec (original + text extract for grep) |
| `docs/jpx-trading-methodology-2024.pdf`, `.extracted.txt` | JPX 2024 Guide to TSE Trading Methodology (Q12 cross-check source) |
| `docs/itayose-calculation.md` | IAP/IAV algorithm reference: 5-condition derivation, edge cases, JPX Q12 cross-check |

## Build

```bash
# One-time system prereqs (Ubuntu/Debian)
sudo apt install -y build-essential cmake zlib1g-dev

# Configure and compile
cmake -S . -B build
cmake --build build -j
```

This produces three binaries under `build/`:

- `tse_mbo` — the CLI that writes the deliverable CSV.
- `tse_mbo_tests` — unit tests.
- `tse_mbo_fixture_tests` — real-capture regression; only built because `data/` contains the sample inputs. If `data/` is ever emptied, CMake silently skips this target (see `CMakeLists.txt:41`).

## Run

Regenerate the committed deliverable from the in-repo sample data:

```bash
./build/tse_mbo \
  --pcap data/20241105_051.test.pcap.gz \
  --pcap data/20241105_052.test.pcap.gz \
  --venue-json data/TseVenue.20241105.json \
  --csv-out output/iap_iav_20241105.csv \
  --summary-only
```

Flags:

- `--pcap <path>` — repeat once per capture. Files are timestamp-merged across `--pcap` args, so order on the command line does not matter.
- `--venue-json <path>` — venue catalog. The `securityType` field filters stocks (`01`–`04`) and `basePrice` seeds the Itayose reference price `R`.
- `--csv-out <path>` — deliverable CSV destination. Omit it to print the run summary without writing.
- `--summary-only` — suppress the per-issue live-order previews that otherwise stream to stdout during replay.

Full test suite:

```bash
ctest --test-dir build --output-on-failure
```

The fixture test additionally writes the audit CSVs described in the [Output](#output) section to `build/results/`.

## Output

The deliverable CSV uses the format requested in the assignment:

```text
symbol,iap,iav
1382,1777.0000,300
1417,2213.0000,24800
...
```

**Row scope.** The venue JSON marks each instrument with a `securityType`; the assignment defines stocks as `securityType` `01-04`, so non-stock issue codes (e.g. `1570` with `securityType=B1`) are replayed for audit but excluded from the CSV. For the supplied 2024-11-05 captures this leaves **304 stock rows**.

**No-result rows.** 32 of the 304 stocks emit `iap=0.0000, iav=0`:

- `10` rows where replay never accumulates any opening-eligible orders (e.g. `1452`, `152A`, `154A`, `158A`).
- `22` rows where the book has orders on both sides but no price satisfies Cond 1 (`cum_bid(P) > 0 AND cum_ask(P) > 0`) — non-crossing books and extreme one-sided imbalances (e.g. `1770`, `2481`, `3600`, `3954`, `6164`).

The remaining **272 rows have a valid IAP with `iav > 0`.**

**Supplementary audit CSVs** are produced by the fixture test in `build/results/`:

- `step1_decoded_messages.csv` — one row per decoded FLEX tag.
- `step1_fixture_report.txt` — counts, endpoints, per-capture issue-code → venue-name mappings.
- `step2_order_book_audit.csv` — final per-price-level audit with `cum_bid`, `cum_ask`, `tip_up`, `tip_down`; the `selected=yes` row explains the chosen IAP/IAV for each symbol.
- `step3_iap_iav_fixture_results.csv` — same per-stock output as the deliverable, regenerated by the test.

## Project layout

```text
src/
  cli/
    main.cpp                     executable entry point; parses argv and calls app::run
  app/
    app.{hpp,cpp}                CLI argument parsing, venue JSON loader, stock filter, CSV output
  replay/
    replay_runner.{hpp,cpp}      loads all PCAPs, timestamp-merges records across files,
                                 dispatches to ReplayDataCallback
  ingest/
    pcap_reader.{hpp,cpp}        gzipped classic PCAP reader (little-endian, microsecond or nanosecond)
    network_decoder.{hpp,cpp}    Ethernet + IPv4 + UDP frame decoder; produces UdpDatagramView
  flex/
    flex_parser.{hpp,cpp}        FLEX packet header + variable-length tag splitter
    flex_message.{hpp,cpp}       typed A/D/E/C/R decoder; price sentinel and market-order handling
  tse/
    tse.{hpp,cpp}                thin facade over OrderBookReplayer; implements ReplayDataCallback
  book/
    order_book.{hpp,cpp}         per-issue state, opening-eligible ladder, live_orders map,
                                 A/D/E/C/R replay, base-price + reference-price bookkeeping
    indicative.{hpp,cpp}         Itayose 5-condition rule (Cond 1 guard, band filter,
                                 min-imbalance, side rule, reference tie-break)
tests/
  test_main.cpp                  unit tests: PCAP reader, UDP decoder, FLEX splitter,
                                 order-book replay, every Itayose branch incl. no-result cases
  fixture_capture_test.cpp       real-capture regression; asserts exact counts and writes
                                 step1/step2/step3 audit CSVs to build/results/
  audit/
    iap_iav_audit.hpp            header-only audit-CSV writer reused by the fixture test
    compare_step1_step2_order_book.py   cross-check: replay step1 rows independently in
                                        Python, diff against step2 state
    trace_symbol_order_book.py   per-symbol order-by-order trace verifier
docs/
  assignment-original.docx       original take-home assignment (as supplied)
  protocol-original.{docx,pdf}   original FLEX Full MBO protocol specification
  protocol-source.md             text extract of the protocol spec for in-repo search
  jpx-trading-methodology-2024.pdf            JPX 2024 Guide to TSE Trading Methodology
  jpx-trading-methodology-2024.extracted.txt  its text extract (used for the Q12 cross-check)
  itayose-calculation.md         the IAP/IAV algorithm reference: 5-condition hierarchy,
                                 JPX Q12 cross-check, edge cases, outstanding work
output/
  iap_iav_20241105.csv           assignment deliverable: symbol,iap,iav for 304 stocks
  README.md                      deliverable description and row-count breakdown
data/
  20241105_051.test.pcap.gz      sample FLEX Full MBO capture (session 051)
  20241105_052.test.pcap.gz      sample FLEX Full MBO capture (session 052)
  TseVenue.20241105.json         venue catalog (securityType + basePrice per issue)
CMakeLists.txt                   build configuration; also wires the fixture test only when
                                 the sample PCAPs and venue JSON are present in data/
```

## Tests

Two test executables, both run by `ctest --test-dir build --output-on-failure`:

- **`tse_mbo_unit_tests`** — synthetic coverage for the PCAP reader, UDP decoder, FLEX splitting, order-book replay (`A`/`D`/`E`/`C`/`R`), and the full Itayose 5-condition hierarchy (including Cond 3 vs Cond 5 conflict, all three Cond 5 branches, equidistance tie-break, and the no-result cases).
- **`tse_mbo_fixture_tests`** — full real-capture regression: reads both sample PCAPs from `data/`, replays everything, writes the four audit CSVs above, and asserts exact counts (capture records, decoded tags per type, issue counts, indicative row count). Only compiled when the sample inputs exist in `data/` (see `CMakeLists.txt:41`).

## The Itayose rule

Step 3 implements the JPX Itayose call-auction price-formation rule. The matching engine applies five conditions in strict priority order — each only narrows the set left by the previous one. Full derivation, edge cases, and the JPX 2024 Trading Methodology Guide Q12 cross-check are in [`docs/itayose-calculation.md`](docs/itayose-calculation.md).

1. **Cond 1 — Executable price set.** Keep prices `P` where `cum_bid(P) > 0 AND cum_ask(P) > 0`. Non-crossing books and single-sided ladders fail Cond 1 everywhere and return no result.
2. **Cond 2 — Maximum executable volume.** Keep prices that maximise `min(cum_bid, cum_ask)`. Equivalent to `tip_up(P) >= 0 AND tip_down(P) >= 0`, the "Itayose band". IAV is constant within the band.
3. **Cond 3 — Minimum imbalance.** Keep prices that minimise `|cum_bid(P) - cum_ask(P)|`.
4. **Cond 4 — Side rule.** If the Cond 3 set has the same residual side at every price: all-sell → lowest, all-buy → highest. Otherwise → Cond 5.
5. **Cond 5 — Reference-price tie-break.** With `R` = base price: `R > H` → highest, `L ≤ R ≤ H` → closest to `R` (equidistance prefers the higher price for opening), `R < L` → lowest.
6. **IAV** = `min(cum_bid(P*), cum_ask(P*))`.

`R` is the venue JSON `basePrice` for the issue, set once when first observed and never overwritten by the running IAP. On an `R` (Reset) tag the running book is cleared and `R` is restored from `basePrice`.

The JPX 2024 Trading Methodology Guide Q12 lists three opening-price requirements and our implementation maps onto them as: (a) all market orders executed — `cum_*` is seeded with `market_*_volume`; (b) all better-priced limit orders executed — handled by cumulative sums; (c) one side fully executed at `P` — exactly the Cond 2 band, with the Cond 1 guard excluding degenerate "zero-share fully executed" boundaries.

## Assumptions and limits

- Input capture format is gzipped classic PCAP (little-endian), not PCAP-NG.
- Network decoder targets Ethernet + IPv4 + UDP frames.
- A single UDP payload may contain multiple FLEX packets back-to-back; the parser handles this.
- Order-book replay is implemented for `A`, `D`, `E`, `C`, and `R`. `T`/`O`/`K`/`L`/`II`/`BP`/`MG` are not decoded into structured messages — they are seen as raw tag bytes but do not change book state.
- FLEX `Bn` prices are decoded once when `A` tags are replayed: the unsigned wire integer uses four implicit decimal places, so raw `17770000` becomes real price `1777.0000`. The 64-bit market-order sentinel is excluded from the limit-price ladder.

## What is not implemented

Documented for completeness; none are needed to produce the assignment's output on the supplied PCAPs:

- **FLEX `BP` tag decoding** for intra-day base-price updates (special quote / sequential trade quote regime). The supplied captures contain no BP tags, so static `basePrice` from the venue JSON is sufficient here.
- **Halt / special-quote / sequential-trade-quote state machine.** Would require typed `O` and `BP` tag decoding.
- **Closing-auction pre-closing rules** (on-close / Funari order transitions). Out of scope for an opening-auction deliverable.
- **Per-securities-company allocation among orders at `P*`.** The JPX rule allocates by aggregated quantity per securities company, but the FLEX MBO feed carries no participant identifier on the wire — this rule cannot be implemented from PCAP-only data. Documented in `docs/itayose-calculation.md` §3 step 7.
- **Tick-size and lot-size validation against the venue JSON.** Each `TseFullInstrument` carries `tickSizeTable` (ID `1` or `3`, referencing JPX's published tick schedules) and `unitOfTrading` (`1` / `10` / `100` / `1000`), and the assignment doc calls these out as available in the JSON. We deliberately do not consume them: every candidate IAP is a price that already appeared on the wire as an `A`-tag limit, so it is on-tick by construction — the exchange would not have accepted it otherwise — and every quantity summed into `cum_bid` / `cum_ask` came straight off the wire, so the lot-size invariant cannot be broken by our arithmetic. Adding both checks would be defensive instrumentation against decoder bugs or feed corruption rather than an algorithmic requirement of the Itayose rule, and would not change the deliverable CSV on the supplied captures. Tick-size validation would additionally require encoding the JPX Table 1 and Table 3 grids (a yen-step schedule over ~13 price bands each) from the JPX Trading Methodology PDF, since the JSON only stores the table ID. We load only `exchSymbol`, `securityType`, and `basePrice` from the venue JSON (`src/app/app.cpp:80`).
