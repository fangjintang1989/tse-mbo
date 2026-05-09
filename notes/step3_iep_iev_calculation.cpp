// Step 3 IEP/IEV reference note.
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
// 1. Initialize the level book from the message.
// 2. Start from previous_reference_price when it exists.
// 3. At each price, read bid_volume, ask_volume, cum_bid, and cum_ask.
// 4. Compute:
//    tip_up = cum_ask - cum_bid + bid_volume
//    tip_down = cum_bid - cum_ask + ask_volume
// 5. If both tips are positive, accept the current price as IEP.
// 6. Set IEV = min(cum_bid, cum_ask).
// 7. If the search direction reverses, keep the closer candidate by comparing
//    abs(previous_tip_up + previous_tip_down) with abs(tip_up + tip_down).
// 8. If the search hits the beginning or end of the price ladder, return the
//    current price and IEV.
