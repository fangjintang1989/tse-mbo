# TSE FLEX MBO Historical Processor

Initial C++20 scaffold for the take-home assignment: process historical PCAP captures of the Tokyo Stock Exchange FLEX Full MBO feed and produce the final indicative open auction price and volume for each stock.

## Current Status

This repository is intentionally an initial project skeleton, not the finished solution yet.

What is included:

- a conventional CMake-based C++ project layout
- starter domain types for packet, tag, and order-book state
- a small CLI that validates input arguments
- Markdown notes that restate the assignment and protocol specification in plain language

What is not implemented yet:

- gzip decompression of the provided `.pcap.gz` files
- PCAP frame parsing
- UDP multicast payload decoding
- tag-by-tag reconstruction of the FLEX MBO stream
- indicative open auction calculation
- CSV output generation

## Assignment Inputs

The original assignment files currently live one directory above this project:

- `../Take home coding assignment.docx`
- `../protocol_spec.pdf`
- `../TseVenue.20241105.json`
- `../20241105_051.test.pcap.gz`
- `../20241105_052.test.pcap.gz`

Supporting understanding docs inside this project:

- [Assignment Understanding](docs/assignment-understanding.md)
- [Protocol Understanding](docs/protocol-understanding.md)

## Proposed CLI

```bash
./tse_flex_mbo_historical_processor \
  --pcap ../20241105_051.test.pcap.gz \
  --pcap ../20241105_052.test.pcap.gz \
  --venue-json ../TseVenue.20241105.json \
  --output out/final_indicative_open.csv
```

## Build

Target toolchain:

- C++20
- CMake 3.20+
- GCC 12+ or Clang 16+

Example build commands on a machine with a compiler installed:

```bash
cmake -S . -B build
cmake --build build
```

## Environment Notes

Observed local environment for this scaffold:

- OS: Ubuntu 26.04 LTS on WSL2
- Kernel: Linux 6.6.87.2-microsoft-standard-WSL2

Important limitation:

- this workspace does not currently have `cmake`, `g++`, or `make` installed, so the code was structured for later build verification rather than compiled here

## Suggested Implementation Order

1. Add gzip input support and a small PCAP reader.
2. Decode Ethernet, IPv4, and UDP payloads into raw FLEX packets.
3. Parse the 26-byte packet header and iterate each tag in sequence.
4. Maintain per-issue order-book state keyed by issue code and per-order state keyed by order ID.
5. Apply `A`, `D`, `E`, `C`, and `R` tags to reconstruct the book.
6. Track auction-relevant state and compute the last indicative open auction price and volume for each stock.
7. Join venue metadata from `TseVenue.20241105.json` and export `symbol,iap,iav`.

## Repository Layout

```text
.
├── CMakeLists.txt
├── README.md
├── docs
│   ├── assignment-understanding.md
│   └── protocol-understanding.md
├── include
│   └── tse_flex_mbo
│       ├── app_config.hpp
│       ├── market_data.hpp
│       └── order_book.hpp
└── src
    ├── main.cpp
    └── order_book.cpp
```

