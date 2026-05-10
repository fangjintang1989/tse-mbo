# JPX Transaction Methods Reference

Sources:
- [Transaction Methods | Trading Rules of Domestic Stocks | Japan Exchange Group](https://www.jpx.co.jp/english/equities/trading/domestic/04.html)
- [Guide to TSE Trading Methodology 2024 Edition (PDF)](https://www.jpx.co.jp/english/equities/trading/domestic/tvdivq000000tpfi-att/eg.pdf)
- [Local PDF copy](/home/jason/codingAssignment/tse-mbo/docs/jpx-trading-methodology-2024.pdf)

JPX page update date: November 4, 2024. Local project note added: May 10, 2026.

This note paraphrases the official JPX/TSE page so future IAP/IAV work has a stable in-repo reference. The official page remains the source of truth.

## Relevance to IAP/IAV

The page is relevant to the assignment because it describes the trading-methodology context for opening auctions:

- TSE uses the Itayose method for opening prices.
- Itayose is a call-auction method that determines one matching price.
- Orders placed before the opening price is determined are treated as simultaneous orders.
- Market orders have priority over limit orders.
- Price priority means lower sell orders and higher buy orders have priority.
- Time priority is normally used among orders at the same price, but it is not applied to pre-opening simultaneous orders.

The page gives the business-rule context for opening-auction behavior. It does not provide the exact `tip_up` / `tip_down` implementation formula used by this project; that formula is captured separately in `notes/step3_iap_iav_calculation.cpp`.

## Opening-Price Calculation Method

The official guide PDF describes opening-price formation with these rules:

- Opening prices are determined by the Itayose method.
- The target price must satisfy three conditions:
  - all market orders are executed
  - all better-priced limit orders are executed
  - at the execution price, all orders on one side are fully executed
- The opening price is searched among prices where aggregated bids and offers invert.
- Orders submitted before the opening price is determined are treated as simultaneous orders.
- Simultaneous orders ignore time priority and are allocated by securities company in descending aggregate quantity.
- If two securities companies have the same quantity, the one with earlier first-arrival time gets priority.

This is the official rule context I should use when checking the step 3 matcher. The PDF still does not spell out the exact `tip_up` / `tip_down` algebra in the same notation as our implementation note, so the current code note remains the direct algorithm reference.

## Order Types

The page describes two main order types:

- Limit orders: buy or sell orders constrained by a stated price.
- Market orders: buy or sell orders that can execute at available prices.

It also describes execution conditions that may be attached to orders:

- On open: executable only during the opening auction.
- On close: executable only during the closing auction.
- Funari: limit orders that become market orders at the closing auction if not already executed.
- IOC: immediate-or-cancel orders.

## Priority Rules

The page describes price priority and time priority:

- Price priority: lower sell prices and higher buy prices take priority.
- Market orders take priority over all limit orders.
- Time priority: for orders at the same price, the earlier order normally has priority.

For opening auctions, the page states that time priority is ignored for orders submitted before the opening price is determined. Those orders are treated as simultaneous orders.

## Itayose vs Zaraba

The page describes two price-determination methods:

- Itayose method: call-auction method.
- Zaraba method: continuous-trading method.

Itayose is used to determine:

- opening prices for each session
- closing prices for each session
- initial prices after trading resumes from a halt
- prices when a special quote or sequential trade quote is indicated

Zaraba is used outside those Itayose situations.

## Special Quote and Sequential Trade Quote Context

The page explains that special quotes and sequential trade quotes are mechanisms for handling sharp price moves or order imbalances. This matters for MBO interpretation because the protocol has tags and fields related to quote display and Itayose execution.

For this assignment, the main connection is that `C` tags and trading-status fields should be interpreted with auction and quote-display context in mind.

## Closing Auction Notes

The page also describes closing-auction behavior, including:

- pre-closing order acceptance
- registration of on-close and Funari orders
- Itayose at the closing time
- special handling for closing auctions at daily limit prices
- special execution rules when a normal closing Itayose cannot form a price

These rules are useful context but are not the same as a complete formula for the assignment's final indicative opening-auction price and volume.

## Project Interpretation

For this project:

- Use this JPX page as the official source for the high-level TSE auction methodology.
- Use `docs/protocol-source.md` and `docs/protocol-summary.md` for FLEX MBO packet and tag semantics.
- Use `notes/step3_iap_iav_calculation.cpp` as the current implementation note for the IAP/IAV selection algorithm.
- Keep tests around edge cases, because the public JPX page does not spell out the detailed `tip_up` / `tip_down` rule.
