# Implementation Plan

## Step 1: PCAP ingestion

Target output of step 1:

- a sequence of decoded UDP payloads with capture timestamp metadata

Subtasks:

1. read `.pcap.gz`
2. parse classic PCAP records
3. parse Ethernet frames
4. parse IPv4 packets
5. parse UDP datagrams
6. keep only payloads that match the target FLEX feed format

Expected internal types:

- `CaptureRecord`
- `EthernetFrame`
- `Ipv4Packet`
- `UdpDatagram`
- `FlexPacketBytes`

## Step 2: FLEX parsing and order-book replay

Target output of step 2:

- per-issue order-book state reconstructed from the feed

Subtasks:

1. parse FLEX packet header
2. iterate tag records by tag length
3. decode known tag types
4. replay order-book events in exact feed order
5. handle reset semantics
6. track market state and split-packet context

Expected internal types:

- `FlexPacketHeader`
- `FlexTag`
- `AddOrderTag`
- `DeleteOrderTag`
- `ExecutedTag`
- `ExecutedWithPriceTag`
- `ResetTag`
- `TradingStatusTag`
- `ExecutionSummaryTag`
- `OrderBook`
- `IssueState`

## Step 3: IEP/IEV calculation

Implemented using the screenshot-derived rule.

Readable extracted note:

- `notes/step3_iep_iev_calculation.cpp`

Predefine a function boundary now so the rest of the system can be built around it:

```cpp
IndicativeMatchResult calculate_indicative_match(const IssueState& issue_state);
```

Possible return type:

```cpp
struct IndicativeMatchResult {
  bool has_result;
  Price price;
  std::uint64_t volume;
};
```

Current implementation notes:

- `IssueState` keeps opening-eligible price-ladder state plus market-order totals
- raw FLEX `Bn` prices are converted at `A` tag replay from fixed-point integer to real decimal price using four fractional digits
- replay recalculates the rolling per-issue IEP/IEV after every `A`, `D`, `E`, or `C`
- CLI CSV output is `symbol,iep,iev`

## Validation strategy

For step 1:

- count packets
- count UDP datagrams
- inspect a few decoded payload sizes and headers

For step 2:

- count parsed tags by type
- track resets
- dump sample issue snapshots
- verify order counts and price levels evolve sensibly

For step 3:

- compare produced CSV against expected review samples if available
- keep the fixture CSV artifact under `build/results/step3_fixture_results.csv`

## Repository layout

- `src/cli`: executable entrypoint
- `src/app`: application orchestration and output
- `src/ingest`: PCAP and network decoding
- `src/flex`: FLEX parser
- `src/book`: order book and indicative calculation
- `tests`: synthetic unit tests and real-capture fixture regression
- `docs`: assignment/protocol source material
- `notes`: project memory and decisions
