# Protocol Notes

This file is the short working-memory version of the protocol.

## Transport

- UDP multicast
- Ethernet -> IP -> UDP -> tag data
- tag data = packet header + repeated tag length/tag data entries

## Encodings

- `Char`: ASCII, left-aligned
- `Bn`: unsigned big-endian integer
- `Bn (Price)`: unsigned big-endian integer, last 4 digits are decimal fractions

## Packet construction rules

- one tag never spans multiple packets
- one matching process can span multiple packets
- different issues are not mixed in one packet
- different processes are not mixed in one packet

## Packet header fields

- `Multicast Group Number`
- `Number of System Reboots`
- `Sequence Number`
- `Issue Code`
- `Update Number`
- `Packet Number`
- `Total Number of Packets`
- `Utility Flag`
- `Message Count`

## Most important replay tags

- `T`: time anchor
- `O`: trading state
- `A`: add order
- `D`: delete order
- `E`: Zaraba execution
- `C`: Itayose or quote-display execution
- `K`: execution summary
- `R`: reset
- `L`: communication control

## Book reconstruction assumptions

To reconstruct a visible book, the first useful tag set is:

- `A`
- `D`
- `E`
- `C`
- `R`

`O`, `K`, `L`, and `T` should still be parsed and stored because they affect sequencing, market phase, and later auction logic.

## Order conditions explicitly seen

- `0`: Non-conditional
- `2`: On-open
- `4`: On-close
- `6`: Funari

## Important appendix risks

- split packets during large Itayose or Zaraba events
- `K` can arrive before all related packets finish
- On-close and Funari behavior changes at pre-closing without fresh `A` tags
- some price-limit transformations happen without redistributed `A` tags

## What is still not fully explicit

- exact final indicative opening-auction calculation rule
- whether `II`, `BP`, and `MG` are required for the provided samples
- whether the PCAPs provide a complete enough initial state to build a full book without snapshots
