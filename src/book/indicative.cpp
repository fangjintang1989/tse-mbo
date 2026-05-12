#include "book/indicative.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace tse_mbo {

// The Itayose call-auction rule selects at most one contract price per call.
// The matching engine applies five conditions in strict priority order and
// each one only narrows the set left by the previous one:
//
//   Cond 1  executable price set     cum_bid(P) > 0 AND cum_ask(P) > 0
//   Cond 2  maximum executable vol   max over P of min(cum_bid(P), cum_ask(P))
//   Cond 3  minimum imbalance        min over P of |cum_bid(P) - cum_ask(P)|
//   Cond 4  same-side tie-break      all-sell -> lowest, all-buy -> highest
//   Cond 5  reference-price tie-break  against previous close / base price
//
// This file is the one place those five conditions are applied. See
// docs/itayose-calculation.md for the full derivation, the JPX 2024
// Trading Methodology Guide Q12 cross-check, and the edge-case playbook.

namespace {

// Per-price row we build once from IssueState and then mutate with cumulative
// sums. `cum_bid` accumulates from the top of the book down, `cum_ask` from
// the bottom up, so at any P they answer "how many shares can fill at >= P
// on the buy side" and "how many shares can fill at <= P on the sell side".
struct PriceSnapshot {
  Price price = 0;
  Volume bid_volume = 0;
  Volume ask_volume = 0;
  Volume cum_bid = 0;
  Volume cum_ask = 0;
};

// Step 6 of the rule: IAV at the chosen price is exactly the minimum of the
// two cumulative sides. By construction, inside the Cond 2 band this value
// is constant, so the value we report is unambiguous.
IndicativeMatchResult make_result(const PriceSnapshot& snapshot) {
  IndicativeMatchResult result;
  result.has_result = true;
  result.price = snapshot.price;
  result.volume = std::min(snapshot.cum_bid, snapshot.cum_ask);
  return result;
}

}  // namespace

IndicativeMatchResult calculate_indicative_match(const IssueState& issue_state) {
  // ------------------------------------------------------------------
  // Guard 0: empty book. No limit orders and no market orders on either
  // side means there is nothing to match, so we bail out immediately.
  // Note we check this before building snapshots to avoid unnecessary
  // allocation on the hot path (this function runs once per A/D/E/C tag).
  // ------------------------------------------------------------------
  const auto& price_levels = issue_state.limit_price_levels;
  if (price_levels.empty() && issue_state.market_bid_volume == 0 && issue_state.market_ask_volume == 0) {
    return {};
  }

  // ------------------------------------------------------------------
  // Build the per-price snapshot vector in ascending price order.
  // `limit_price_levels` is a std::map so iteration is already sorted,
  // which is exactly what the cumulative-sum passes below require.
  // ------------------------------------------------------------------
  std::vector<PriceSnapshot> snapshots;
  snapshots.reserve(price_levels.size());
  for (const auto& [price, level] : price_levels) {
    snapshots.push_back(PriceSnapshot{
        .price = price,
        .bid_volume = level.bid_volume,
        .ask_volume = level.ask_volume,
    });
  }

  // Guard 1: if the book has only market orders (no limits), there is no
  // candidate price to form a contract price at, so no IAP exists.
  if (snapshots.empty()) {
    return {};
  }

  // ------------------------------------------------------------------
  // Cumulative ask: low-to-high sweep.
  // Seed with market_ask_volume so market orders are absorbed first,
  // matching JPX Q12(a) "all market orders must be executed".
  // After this loop, cum_ask at price P = market_ask_volume + sum of
  // limit ask volumes at prices <= P.
  // ------------------------------------------------------------------
  Volume running_ask = issue_state.market_ask_volume;
  for (auto& snapshot : snapshots) {
    running_ask += snapshot.ask_volume;
    snapshot.cum_ask = running_ask;
  }

  // ------------------------------------------------------------------
  // Cumulative bid: high-to-low sweep (reverse iteration).
  // Seed with market_bid_volume for the same reason as above.
  // After this loop, cum_bid at price P = market_bid_volume + sum of
  // limit bid volumes at prices >= P.
  // ------------------------------------------------------------------
  Volume running_bid = issue_state.market_bid_volume;
  for (std::size_t index = snapshots.size(); index > 0; --index) {
    auto& snapshot = snapshots[index - 1];
    running_bid += snapshot.bid_volume;
    snapshot.cum_bid = running_bid;
  }

  // ==================================================================
  // Cond 1 + Cond 2: the Itayose band.
  //
  // Cond 1 requires BOTH cumulative sides to be strictly positive at P
  // (there must be something to match on each side). A common pitfall
  // is skipping this check and relying on the tip filter alone: on a
  // single-sided or non-crossing book the band can still contain
  // boundary prices where one side has zero volume, which would give
  // IAV = 0 and a bogus boundary IAP. We explicitly exclude those.
  //
  // Cond 2 keeps prices that maximise min(cum_bid, cum_ask). In a
  // static ladder, that set equals {P : tip_up(P) >= 0 AND tip_down(P) >= 0}
  // where
  //   tip_up(P)   = cum_ask(P) - cum_bid(P) + bid_at_P
  //   tip_down(P) = cum_bid(P) - cum_ask(P) + ask_at_P
  // tip_up >= 0 means "everything at <= P on the ask side can be filled
  // by cum_bid plus the bids sitting exactly at P". tip_down is the
  // symmetric statement. Within the band, min(cum_bid, cum_ask) is
  // constant: that constant is the IAV we eventually report.
  //
  // Uses `>=` (not `>`) so band-edge prices where one side fully clears
  // with zero residual are included, per the official rule.
  // ==================================================================
  std::vector<const PriceSnapshot*> band;
  band.reserve(snapshots.size());
  for (const auto& snapshot : snapshots) {
    // Cond 1 guard.
    if (snapshot.cum_bid == 0 || snapshot.cum_ask == 0) {
      continue;
    }
    const std::int64_t tip_up = static_cast<std::int64_t>(snapshot.cum_ask) -
                                static_cast<std::int64_t>(snapshot.cum_bid) +
                                static_cast<std::int64_t>(snapshot.bid_volume);
    const std::int64_t tip_down = static_cast<std::int64_t>(snapshot.cum_bid) -
                                  static_cast<std::int64_t>(snapshot.cum_ask) +
                                  static_cast<std::int64_t>(snapshot.ask_volume);
    // Cond 2 band condition.
    if (tip_up >= 0 && tip_down >= 0) {
      band.push_back(&snapshot);
    }
  }

  // Empty band => extreme imbalance or non-crossing book. The correct
  // output per the JPX rule is "no IAP", NOT whichever boundary price
  // the search happened to land on.
  if (band.empty()) {
    return {};
  }

  // ==================================================================
  // Cond 3: among band prices, keep only those with the minimum
  // |cum_bid - cum_ask|. This matters because Cond 2 can leave several
  // prices in the band with the same IAV but different imbalance; the
  // rule requires the smallest residual imbalance to win before the
  // reference-price tie-break runs. Skipping Cond 3 and jumping straight
  // to "closest to base" is a silently-wrong shortcut that agrees with
  // the rule on most symbols and disagrees on ~14% of real data.
  // ==================================================================
  std::int64_t min_imbalance = std::numeric_limits<std::int64_t>::max();
  for (const auto* snapshot : band) {
    const std::int64_t imbalance = std::abs(static_cast<std::int64_t>(snapshot->cum_bid) -
                                            static_cast<std::int64_t>(snapshot->cum_ask));
    if (imbalance < min_imbalance) {
      min_imbalance = imbalance;
    }
  }

  // Collect all band prices that achieve min_imbalance. We preserve the
  // ascending-price order from the original ladder, which later lets us
  // use `cond3.front()` = lowest price and `cond3.back()` = highest.
  std::vector<const PriceSnapshot*> cond3;
  cond3.reserve(band.size());
  for (const auto* snapshot : band) {
    const std::int64_t imbalance = std::abs(static_cast<std::int64_t>(snapshot->cum_bid) -
                                            static_cast<std::int64_t>(snapshot->cum_ask));
    if (imbalance == min_imbalance) {
      cond3.push_back(snapshot);
    }
  }

  // Default pick: if Cond 3 narrowed to exactly one price, we're done;
  // Cond 4 and Cond 5 are only needed when multiple prices remain tied.
  const PriceSnapshot* chosen = cond3.front();
  if (cond3.size() > 1) {
    // --------------------------------------------------------------
    // Cond 4: side-of-imbalance tie-break.
    //
    // signed_imb = cum_ask - cum_bid. Positive => sell-side residual
    // (more sellers at this P), negative => buy-side residual.
    //
    // A bit of algebra: within the Cond 2 band, as P increases,
    // cum_ask never decreases and cum_bid never increases, so
    // signed_imb is monotonically non-decreasing. Therefore if the
    // minimum |signed_imb| over Cond 3 is > 0, every Cond 3 price
    // shares the same sign (all sell or all buy). If the minimum is
    // exactly zero, the band crosses zero at one or more prices and
    // we drop to Cond 5. The booleans below capture this.
    //
    // When Cond 4 applies:
    //   all sell-side residual => pick lowest price (reduces residual
    //     the most on the sell side)
    //   all buy-side residual  => pick highest price (symmetric)
    // --------------------------------------------------------------
    bool all_sell_side = true;
    bool all_buy_side = true;
    for (const auto* snapshot : cond3) {
      const std::int64_t signed_imb = static_cast<std::int64_t>(snapshot->cum_ask) -
                                      static_cast<std::int64_t>(snapshot->cum_bid);
      if (signed_imb <= 0) {
        all_sell_side = false;  // found a buy-side or zero-residual P
      }
      if (signed_imb >= 0) {
        all_buy_side = false;   // found a sell-side or zero-residual P
      }
    }

    if (min_imbalance > 0 && all_sell_side) {
      chosen = cond3.front();  // lowest price; snapshots ascending by price
    } else if (min_imbalance > 0 && all_buy_side) {
      chosen = cond3.back();   // highest price
    } else {
      // ------------------------------------------------------------
      // Cond 5: reference-price tie-break.
      //
      // R = base price. Source-of-truth here is IssueState.previous_
      // reference_price, which is bootstrapped once per issue from
      // the venue JSON basePrice and reset back to basePrice on an R
      // tag. Crucially it is NOT overwritten by the running IAP --
      // that would conflate three different concepts (previous-day
      // close, running indicative, last execution) and silently move
      // the anchor off-rule.
      //
      // value_or(0) is a harmless default because this branch is
      // only reached for issues the venue JSON covers, so the
      // reference price is always set in practice.
      //
      // Three sub-rules:
      //   5.1 R > H          -> highest
      //   5.2 L <= R <= H    -> closest to R; on exact equidistance
      //                         prefer the HIGHER price (opening
      //                         convention; closing would prefer
      //                         lower). Implemented by `<=` below so
      //                         later (higher) ties win.
      //   5.3 R < L          -> lowest
      // ------------------------------------------------------------
      const Price reference_price = issue_state.previous_reference_price.value_or(0);
      const Price highest = cond3.back()->price;
      const Price lowest = cond3.front()->price;
      if (reference_price > highest) {
        chosen = cond3.back();
      } else if (reference_price < lowest) {
        chosen = cond3.front();
      } else {
        const PriceSnapshot* best = cond3.front();
        std::int64_t best_distance = std::abs(best->price - reference_price);
        for (const auto* snapshot : cond3) {
          const std::int64_t distance = std::abs(snapshot->price - reference_price);
          if (distance <= best_distance) {
            best = snapshot;
            best_distance = distance;
          }
        }
        chosen = best;
      }
    }
  }

  // ------------------------------------------------------------------
  // Step 6: compute IAV and stash the reference price we used, if any,
  // so callers / audit CSVs can show their work.
  // ------------------------------------------------------------------
  IndicativeMatchResult result = make_result(*chosen);
  if (issue_state.previous_reference_price.has_value()) {
    result.has_reference_price = true;
    result.reference_price = *issue_state.previous_reference_price;
  }
  return result;
}

}  // namespace tse_mbo
