// Step 3 IAP/IAV reference note.
// This is the cleaned, in-repo version of the screenshot transcription.
// The raw transcription now lives outside the repo in /home/jason/iep_pic/.

// Naming map
// - previous_reference_price
// - price_ladder
// - price_cursor
// - bid_price
// - bid_volume
// - ask_price
// - ask_volume
// - cum_bid
// - cum_ask
// - tip_up
// - tip_down
// - previous_candidate_price
// - previous_tip_up
// - previous_tip_down
// - previous_search_direction
// - search_direction
// - indicative_equilibrium_volume

// Algorithm
// 1. Build cum_bid and cum_ask across the limit price ladder, including
//    market_bid_volume and market_ask_volume on the appropriate sides.
// 2. For each price P in the ladder compute:
//    tip_up   = cum_ask - cum_bid + bid_volume
//    tip_down = cum_bid - cum_ask + ask_volume
// 3. A price is in the TSE Itayose band when tip_up >= 0 and tip_down >= 0.
//    This is equivalent to the JPX rule that at P:
//      - all market orders execute,
//      - all limit orders strictly superior to P execute,
//      - at least one whole side at P fully executes.
// 4. Among in-band prices, pick the one with the smallest
//    abs(price - previous_reference_price). Ties keep the lower price.
// 5. IAV at the chosen price = min(cum_bid, cum_ask).
// 6. If no price is in-band, return has_result=false. The CSV writer renders
//    those rows as "0.0000,0".
