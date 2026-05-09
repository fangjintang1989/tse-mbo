# Decision Log

## 2026-05-08

- Created repo-local `notes/` directory to act as persistent working memory.
- Agreed to break the assignment into 3 steps:
  1. get PCAP data
  2. parse to order book
  3. calculate indicative equilibrium price and volume (IEP/IEV)
- Agreed to defer step 3 until the user provides the exact calculation rule.
- Chose to focus first on step 1 and step 2 in C++.
- Decided to predefine a calculation function boundary before implementing the final logic.
- Chose to keep protocol interpretation and open questions in repo docs instead of relying on hidden assistant memory.

## 2026-05-09

- Added a readable step 3 note so the screenshot transcription can be reviewed with clearer IEP/IEV naming.
- Implemented rolling step 3 IEP/IEV calculation on top of replayed opening-eligible book state.
- Added CSV export for `symbol,iep,iev` plus real-capture fixture output for step 3.
- Decided all downstream order-book, IEP/IEV, fixture, and CSV prices use real decimal prices. Raw FLEX `Bn` prices are converted once at replay decode using four fractional digits.
- Cleaned the repository layout to a single `src/` code root and removed generated build output, the large generated PCAP dump log, and empty legacy scaffolding directories.
