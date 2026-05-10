#include "book/indicative.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace tse_mbo {

namespace {

struct PriceSnapshot {
  Price price = 0;
  Volume bid_volume = 0;
  Volume ask_volume = 0;
  Volume cum_bid = 0;
  Volume cum_ask = 0;
};

IndicativeMatchResult make_result(const PriceSnapshot& snapshot) {
  IndicativeMatchResult result;
  result.has_result = true;
  result.price = snapshot.price;
  result.volume = std::min(snapshot.cum_bid, snapshot.cum_ask);
  return result;
}

}  // namespace

IndicativeMatchResult calculate_indicative_match(const IssueState& issue_state) {
  const auto& price_levels = issue_state.limit_price_levels;
  if (price_levels.empty() && issue_state.market_bid_volume == 0 && issue_state.market_ask_volume == 0) {
    return {};
  }

  std::vector<PriceSnapshot> snapshots;
  snapshots.reserve(price_levels.size());
  for (const auto& [price, level] : price_levels) {
    snapshots.push_back(PriceSnapshot{
        .price = price,
        .bid_volume = level.bid_volume,
        .ask_volume = level.ask_volume,
    });
  }

  if (snapshots.empty()) {
    return {};
  }

  Volume running_ask = issue_state.market_ask_volume;
  for (auto& snapshot : snapshots) {
    running_ask += snapshot.ask_volume;
    snapshot.cum_ask = running_ask;
  }

  Volume running_bid = issue_state.market_bid_volume;
  for (std::size_t index = snapshots.size(); index > 0; --index) {
    auto& snapshot = snapshots[index - 1];
    running_bid += snapshot.bid_volume;
    snapshot.cum_bid = running_bid;
  }

  // Cond 1 + Cond 2: prices where bids and offers match at the maximum executable
  // volume. The (tip_up >= 0 && tip_down >= 0) filter is mathematically equivalent
  // to "max min(cum_bid, cum_ask)" within the price ladder.
  std::vector<const PriceSnapshot*> band;
  band.reserve(snapshots.size());
  for (const auto& snapshot : snapshots) {
    const std::int64_t tip_up = static_cast<std::int64_t>(snapshot.cum_ask) -
                                static_cast<std::int64_t>(snapshot.cum_bid) +
                                static_cast<std::int64_t>(snapshot.bid_volume);
    const std::int64_t tip_down = static_cast<std::int64_t>(snapshot.cum_bid) -
                                  static_cast<std::int64_t>(snapshot.cum_ask) +
                                  static_cast<std::int64_t>(snapshot.ask_volume);
    if (tip_up >= 0 && tip_down >= 0) {
      band.push_back(&snapshot);
    }
  }

  if (band.empty()) {
    return {};
  }

  // Cond 3: among Cond 2 prices, keep those with minimum imbalance |cum_bid - cum_ask|.
  std::int64_t min_imbalance = std::numeric_limits<std::int64_t>::max();
  for (const auto* snapshot : band) {
    const std::int64_t imbalance = std::abs(static_cast<std::int64_t>(snapshot->cum_bid) -
                                            static_cast<std::int64_t>(snapshot->cum_ask));
    if (imbalance < min_imbalance) {
      min_imbalance = imbalance;
    }
  }

  std::vector<const PriceSnapshot*> cond3;
  cond3.reserve(band.size());
  for (const auto* snapshot : band) {
    const std::int64_t imbalance = std::abs(static_cast<std::int64_t>(snapshot->cum_bid) -
                                            static_cast<std::int64_t>(snapshot->cum_ask));
    if (imbalance == min_imbalance) {
      cond3.push_back(snapshot);
    }
  }

  const PriceSnapshot* chosen = cond3.front();
  if (cond3.size() > 1) {
    // Cond 4: classify each Cond 3 price by the side of its imbalance, computed
    // from sell - buy at that price. min_imbalance > 0 means every Cond 3 price
    // has the same residual side; we can pick from extremes directly.
    bool all_sell_side = true;
    bool all_buy_side = true;
    for (const auto* snapshot : cond3) {
      const std::int64_t signed_imb = static_cast<std::int64_t>(snapshot->cum_ask) -
                                      static_cast<std::int64_t>(snapshot->cum_bid);
      if (signed_imb <= 0) {
        all_sell_side = false;
      }
      if (signed_imb >= 0) {
        all_buy_side = false;
      }
    }

    if (min_imbalance > 0 && all_sell_side) {
      chosen = cond3.front();  // lowest price (snapshots are ascending)
    } else if (min_imbalance > 0 && all_buy_side) {
      chosen = cond3.back();   // highest price
    } else {
      // Cond 5: fall back to Reference Price.
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
          if (distance < best_distance) {
            best = snapshot;
            best_distance = distance;
          }
        }
        chosen = best;
      }
    }
  }

  IndicativeMatchResult result = make_result(*chosen);
  if (issue_state.previous_reference_price.has_value()) {
    result.has_reference_price = true;
    result.reference_price = *issue_state.previous_reference_price;
  }
  return result;
}

}  // namespace tse_mbo
