# Open Questions

## Functional

- What exact rule should be used for the final indicative opening match price and volume?
- Do the provided PCAPs start from a clean enough state to reconstruct full books without a separate snapshot bootstrap?
- Are `II`, `BP`, or `MG` required for correctness in the provided samples?

## Technical

- Should packet ordering across the two PCAP files be based on file order, packet timestamp, sequence number, or a combination?
- Do we want to support only the provided sample captures first, or design immediately for general replay?

## Environment

- Do we want to install `cmake` and a compiler on this machine soon so we can compile-test locally?

## Waiting on user

- Step 3 calculation rule for indicative match price and volume
