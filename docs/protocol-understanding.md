# Protocol Understanding

This file is my Markdown restatement of the extracted `protocol_spec.pdf` content. It is not a verbatim copy of every page. It is a structured explanation of the parts that appear relevant to implementing the assignment.

## High-Level Structure

The PDF describes a UDP multicast protocol whose payload contains:

- a fixed packet header
- one or more variable-length tags

The packet distributed on the wire is described as:

- Ethernet header
- IP header
- UDP header
- tag data

## Packet Format

The message format section says tag data is composed of:

- `Packet Header`
- `Tag Length (1)`
- `Tag Data (1)`
- ...
- `Tag Length (n)`
- `Tag Data (n)`

Important notes I extracted:

- one tag is never split across packets
- a single matching process can span multiple packets
- packet splitting is signaled through the utility flag

## Packet Header Fields I Expect To Parse

The PDF lists these packet-header fields:

- `Multicast Group Number`
- `Number of System Reboots`
- `Sequence Number`
- `Issue Code`
- `Update Number`
- `Packet Number`
- `Total Number of Packets`
- `Utility Flag`
- `Message Count`

My current implementation assumption is that `Issue Code`, `Sequence Number`, `Update Number`, and `Utility Flag` will be especially important for state reconstruction and grouping related packets.

## Tags That Look Relevant To The Assignment

### Core time and state tags

- `T`: Seconds Timestamp
- `O`: Trading Status
- `L`: Communication Control
- `R`: Reset

### Order-book mutation tags

- `A`: Add Order
- `D`: Order Delete
- `E`: Order Executed
- `C`: Order Executed with Price
- `K`: Execution Summary

### Metadata tags mentioned but probably secondary at first

- `II`: Issue Information
- `BP`: Base Price Information
- `MG`: Multicast Group Number Information

## Important Tag Payloads

### `A` Add Order

The extracted PDF shows these meaningful fields:

- `Order ID`
- `Side`
- `Quantity`
- `Price`
- `Order Condition`
- `Modification Flag`

Interpretation:

- `A` creates or updates visible order-book state.
- The `Modification Flag` matters because some order modifications preserve time priority and some do not.
- Market orders use the maximum 64-bit price value, so special handling is required.

### `D` Order Delete

The extracted PDF shows:

- `Order ID`
- `Side`
- `Modification Flag`

Interpretation:

- `D` removes or invalidates an existing order.
- The reason can be cancel, modification that changes priority, or expiry.

### `E` Order Executed

The extracted PDF shows:

- `Order ID`
- `Side`
- `Volume`
- `Match ID`

Interpretation:

- `E` reduces remaining quantity for an order.
- The same `Order ID` can appear multiple times for partial executions.

### `C` Order Executed with Price

The extracted PDF shows:

- `Order ID`
- `Side`
- `Volume`
- `Match ID`
- `Execution Price`
- `Adopted Pricing Method`

Interpretation:

- `C` is execution data for Itayose or quote-display cases and includes price.
- This looks directly relevant to auction behavior.

### `K` Execution Summary

The extracted PDF shows:

- `Triggered Side`
- `Total Volume`
- `Total Invalidation`
- `Last Price`
- `Match ID`
- `Best Offer`
- `Best Bid`

Interpretation:

- `K` summarizes execution results and may help validate reconstructed state.
- It may also be useful when detecting auction transitions or comparing reconstructed results against feed summaries.

### `O` Trading Status

The extracted PDF lists:

- `Market Status`
- `Status Flag`
- `Short Selling Status`
- `Pricing Method`
- `Book Center Price`

Interpretation:

- `O` likely tells us when the market is in order acceptance, trading hours, Itayose, pre-closing, and related states.
- The `Pricing Method` and `Book Center Price` may be useful when deciding whether an indicative auction price is currently meaningful.

## Ordering And Priority Rules

The PDF includes tag-priority notes:

1. `T`
2. `O`
3. `K`
4. `D`
5. `E` / `C` / `D` ordering rules around executions and invalidations
6. `A`

My reading is that processing order matters and should follow feed order exactly, even when grouped packets from the same matching process arrive split across multiple packets.

## Edge Cases Called Out In The PDF

The appendix contains several implementation traps:

- one logical matching process can span multiple packets
- `K` tags can be sent early so execution summary is delivered without delay
- `Utility Flag` indicates whether more packets from the same matching process follow
- On-close and Funari orders need special attention around pre-closing
- some `A`-tag state is not redistributed when a newly listed issue gets its initial price or a base price change alters effective limit prices

These notes suggest that a naive add/delete/execute replay may not be enough to compute the final indicative auction state correctly in all cases.

## What I Think This Means For The Solution

At minimum, the final solution probably needs:

1. Per-issue state keyed by issue code.
2. Per-order state keyed by order ID.
3. Strict replay in message order.
4. Correct handling of split packets and utility-flag grouping.
5. Reset handling that clears prior valid `A` state.
6. Auction-aware logic for interpreting `O`, `C`, `K`, and the appendix rules.

## Verification Excerpts

These short excerpts are the main evidence behind the summary above:

> 3 Message Specifications for UDP Multicast

> The packet distributed consists of an Ethernet header, IP header, UDP header and tag data.

> 3.2.1 Packet Header

> Message Count 25 1 Bn Set to the number of tags in the packet.

> A tag Add Order 26 - Provides information about book registration.

> E tag Order Executed 20 - Provides information about registered orders in case of Zaraba execution.

> C tag Order Executed with Price 29 - Provides information about registered orders in case of Itayose execution.

> D tag Order Delete 11 - Provides information about order deletion from the order book.

> R tag Reset

> L tag Communication Control

## Parts I Still Treat As Potentially Ambiguous

- the exact tie-break rules for the indicative opening auction price if multiple prices match the same volume
- how much of the final answer can be derived from order-book reconstruction alone versus needing explicit exchange auction-status semantics
- whether `II` or `BP` tags become necessary for a correct final answer rather than just a cleaner model

