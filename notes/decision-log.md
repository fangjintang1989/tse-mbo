# Decision Log

## 2026-05-08

- Created repo-local `notes/` directory to act as persistent working memory.
- Agreed to break the assignment into 3 steps:
  1. get PCAP data
  2. parse to order book
  3. calculate indicative match price and volume
- Agreed to defer step 3 until the user provides the exact calculation rule.
- Chose to focus first on step 1 and step 2 in C++.
- Decided to predefine a calculation function boundary before implementing the final logic.
- Chose to keep protocol interpretation and open questions in repo docs instead of relying on hidden assistant memory.
