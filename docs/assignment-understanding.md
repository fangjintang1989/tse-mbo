# Assignment Understanding

This file is my Markdown restatement of the `Take home coding assignment.docx` document.

## What The Assignment Asks For

- Build a historical PCAP data processing application in C++.
- The feed is the Tokyo Stock Exchange FLEX Full MBO feed.
- Use the attached protocol specification and the provided venue JSON.
- Process UDP packets from multiple PCAP files and maintain a correct order book.
- For the provided samples, output the last calculated indicative open auction match price and quantity/volume for all stocks.
- The expected CSV format is:

```text
symbol,iep,iev
```

## README Expectations

The final submission README is expected to explain at least:

- OS and version used
- compiler and version used
- how to compile
- how to run
- anything else important for the reviewer

## Inputs I Believe Matter

- `20241105_051.test.pcap.gz`
- `20241105_052.test.pcap.gz`
- `protocol_spec.pdf`
- `TseVenue.20241105.json`

The assignment note also says the JSON file helps identify stocks because stocks are defined as security types `1-4`, and the same file contains tick-size and lot-size information.

## My Working Interpretation

I currently interpret the task this way:

1. Reconstruct the multicast message stream from the provided PCAP captures.
2. Parse FLEX MBO packet headers and tags.
3. Maintain per-issue order-book state over time.
4. Focus on auction-relevant state strongly enough to produce the final indicative open auction result for each stock.
5. Export one CSV row per stock with symbol, indicative equilibrium price, and indicative equilibrium volume.

## Verification Excerpts

These are the key lines I extracted from the source document and used for the interpretation above:

> The assignment is developing a historical PCAP data processing application, which handles Tokyo Stock Exchange (TSE)'s Flex Full MBO feed.

> You are required to finish the project, in C++.

> Your code needs to process UDP packets stored in multiple PCAP files, process them and maintain a proper orderbook

> For the given PCAP samples, please provide the last calculated indicative open auction match price and quantity/volume, for all stocks, in a csv file, in the format of "symbol, iap, iav"

Project terminology note: the implementation uses `iep` and `iev` for the output columns because the code and calculation are named around indicative equilibrium price and volume.

## Things Still Not Explicit In The Word Document

- whether packet ordering across multiple PCAP files is strictly file order or timestamp order
- whether the two sample captures overlap or form adjacent parts of one trading day
- the exact expected auction tie-break rules, which likely come from the exchange protocol or related public TSE rules rather than the Word document itself
