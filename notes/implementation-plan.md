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

## Step 3: indicative calculation

Deferred pending user-provided rule.

Predefine a function boundary now so the rest of the system can be built around it:

```cpp
IndicativeMatchResult calculate_indicative_match(const IssueState& issue_state);
```

Possible return type:

```cpp
struct IndicativeMatchResult {
  bool has_result;
  std::uint64_t price;
  std::uint64_t volume;
};
```

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

For step 3 later:

- compare produced CSV against expected review samples if available
