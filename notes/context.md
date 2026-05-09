# Project Context

## Goal

Build a C++ solution for the take-home assignment around historical TSE FLEX Full MBO data.

## Current agreed breakdown

The work is split into 3 steps:

1. get PCAP data into a usable decoded stream
2. parse the decoded feed into an order book
3. calculate indicative auction price and volume (IAP/IAV)

Step 3 now uses the screenshot-derived IAP/IAV rule captured in `notes/step3_iap_iav_calculation.cpp`.

## Current implementation direction

Source code is organized under a single `src/` root:

- `src/cli`: executable entrypoint
- `src/app`: orchestration, arguments, and output
- `src/ingest`: PCAP and network decoding
- `src/flex`: FLEX packet parsing
- `src/book`: order-book replay and IAP/IAV calculation

The code now supports:

- reading the provided `.pcap.gz` files
- decoding Ethernet, IPv4, UDP, and FLEX payloads
- parsing packet headers and tag records
- replaying order-book-relevant tags into per-issue state
- maintaining opening-eligible per-issue ladder state and a rolling IAP/IAV result
- exposing `calculate_indicative_match(const IssueState&)` as the step 3 calculation boundary

## Input files currently available

- `20241105_051.test.pcap.gz`
- `20241105_052.test.pcap.gz`
- `TseVenue.20241105.json`
- `Take home coding assignment.docx`
- `protocol_spec.docx`
- `protocol_spec.pdf`

## Environment constraints

- The repo is `/home/jason/codingAssignment/tse-mbo`
- GitHub remote is configured and working over SSH
- Local build/test is currently available with `cmake 4.2.3` and `g++ 15.2.0`

## Important scope boundary

Current implemented scope:

- step 1: packet ingestion
- step 2: order-book reconstruction
- step 3: rolling IAP/IAV calculation and CSV export
