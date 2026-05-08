# Project Context

## Goal

Build a C++ solution for the take-home assignment around historical TSE FLEX Full MBO data.

## Current agreed breakdown

The work is split into 3 steps:

1. get PCAP data into a usable decoded stream
2. parse the decoded feed into an order book
3. calculate indicative match price and volume

Step 3 is intentionally deferred until the user provides the calculation rule.

## Current implementation direction

Before step 3, the code should support:

- reading the provided `.pcap.gz` files
- decoding Ethernet, IPv4, UDP, and FLEX payloads
- parsing packet headers and tag records
- replaying order-book-relevant tags into per-issue state
- exposing a predefined function that can later calculate indicative match price and volume from the book

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
- This machine currently does not have `cmake` or a C++ compiler installed

## Important scope boundary

For now, we are targeting:

- step 1: packet ingestion
- step 2: order-book reconstruction

We are not yet implementing the final indicative auction calculation logic.
