# Open Questions

## Functional

- Do the provided PCAPs start from a clean enough state to reconstruct full books without a separate snapshot bootstrap?
- Are `II`, `BP`, or `MG` required for correctness in the provided samples?
- Does the current screenshot-derived IAP/IAV rule fully match the reviewer’s expected opening-auction rule in all edge cases?

## Technical

- Should packet ordering across the two PCAP files be based on file order, packet timestamp, sequence number, or a combination?
- Do we want to support only the provided sample captures first, or design immediately for general replay?

## Environment

- None at the moment. Local build and test are working.
