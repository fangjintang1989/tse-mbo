#include "book/indicative.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
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

  std::size_t cursor = 0;
  if (issue_state.previous_reference_price.has_value()) {
    const auto it = std::lower_bound(
        snapshots.begin(), snapshots.end(), *issue_state.previous_reference_price,
        [](const PriceSnapshot& snapshot, Price price) { return snapshot.price < price; });

    if (it == snapshots.end()) {
      cursor = snapshots.size() - 1U;
    } else {
      cursor = static_cast<std::size_t>(std::distance(snapshots.begin(), it));
    }
  }

  std::size_t previous_index = cursor;
  std::int64_t previous_score = 0;
  int previous_direction = 0;

  while (true) {
    const auto& snapshot = snapshots[cursor];
    const std::int64_t tip_up = static_cast<std::int64_t>(snapshot.cum_ask) -
                                static_cast<std::int64_t>(snapshot.cum_bid) +
                                static_cast<std::int64_t>(snapshot.bid_volume);
    const std::int64_t tip_down = static_cast<std::int64_t>(snapshot.cum_bid) -
                                  static_cast<std::int64_t>(snapshot.cum_ask) +
                                  static_cast<std::int64_t>(snapshot.ask_volume);

    if (tip_up > 0 && tip_down > 0) {
      return make_result(snapshot);
    }

    int current_direction = 0;
    if (tip_down <= 0) {
      if (cursor == 0) {
        return make_result(snapshot);
      }
      previous_index = cursor;
      previous_score = static_cast<std::int64_t>(snapshot.bid_volume + snapshot.ask_volume);
      --cursor;
      current_direction = -1;
    } else {
      if (cursor + 1U == snapshots.size()) {
        return make_result(snapshot);
      }
      previous_index = cursor;
      previous_score = static_cast<std::int64_t>(snapshot.bid_volume + snapshot.ask_volume);
      ++cursor;
      current_direction = 1;
    }

    if (previous_direction * current_direction == -1) {
      const auto& current_snapshot = snapshots[cursor];
      const std::int64_t current_score =
          static_cast<std::int64_t>(current_snapshot.bid_volume + current_snapshot.ask_volume);
      if (previous_score < current_score) {
        cursor = previous_index;
      }
      return make_result(snapshots[cursor]);
    }

    previous_direction = current_direction;
  }
}

}  // namespace tse_mbo
