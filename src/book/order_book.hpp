#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "flex/flex_message.hpp"

namespace tse_mbo {

using Price = std::int64_t;
using Volume = std::uint64_t;
using RawPrice = std::uint64_t;
inline constexpr Price kMarketOrderPrice = -1;
inline constexpr RawPrice kRawMarketOrderPrice = std::numeric_limits<RawPrice>::max();
inline constexpr Price kPriceScale = 10000;

inline constexpr Price make_price(std::int64_t whole, std::int64_t fractional_4dp = 0) noexcept {
  const Price max_whole = std::numeric_limits<Price>::max() / kPriceScale;
  const Price min_whole = std::numeric_limits<Price>::min() / kPriceScale;
  if (whole > max_whole) {
    return std::numeric_limits<Price>::max();
  }
  if (whole < min_whole) {
    return std::numeric_limits<Price>::min();
  }

  const Price base = whole * kPriceScale;
  if (fractional_4dp > 0 && base > std::numeric_limits<Price>::max() - fractional_4dp) {
    return std::numeric_limits<Price>::max();
  }
  if (fractional_4dp < 0 && base < std::numeric_limits<Price>::min() - fractional_4dp) {
    return std::numeric_limits<Price>::min();
  }
  return base + fractional_4dp;
}

inline constexpr double price_to_double(Price price) noexcept {
  return static_cast<double>(price) / static_cast<double>(kPriceScale);
}

struct IndicativeMatchResult {
  bool has_result = false;
  Price price = 0;
  Volume volume = 0;
};

enum class Side : char {
  buy = 'B',
  sell = 'S',
  unknown = '?',
};

struct Order {
  std::uint32_t order_id = 0;
  Side side = Side::unknown;
  Volume quantity = 0;
  Price price = 0;
  std::uint8_t order_condition = 0;
  std::uint8_t modification_flag = 0;
};

struct PriceLevel {
  Volume bid_volume = 0;
  Volume ask_volume = 0;
};

struct IssueState {
  std::string issue_code;
  std::unordered_map<std::uint32_t, Order> live_orders;
  std::map<Price, PriceLevel> limit_price_levels;
  Volume market_bid_volume = 0;
  Volume market_ask_volume = 0;
  std::optional<Price> previous_reference_price;
  IndicativeMatchResult last_indicative_match;
  std::uint32_t last_sequence_number = 0;
  std::uint32_t last_update_number = 0;
  Volume seen_tag_count = 0;
};

struct ReplayStats {
  Volume packets_seen = 0;
  Volume packets_parsed = 0;
  Volume tags_seen = 0;
  Volume add_tags = 0;
  Volume delete_tags = 0;
  Volume executed_tags = 0;
  Volume executed_with_price_tags = 0;
  Volume reset_tags = 0;
};

class OrderBookReplayer {
 public:
  void apply(const NormalizedFlexPacket& packet);

  const std::unordered_map<std::string, IssueState>& issues() const noexcept;
  const ReplayStats& stats() const noexcept;

 private:
  void apply_message(IssueState& issue_state, const FlexMessage& message);
  static bool is_market_price(Price price) noexcept;
  static bool is_opening_eligible(const Order& order) noexcept;
  void recalculate_issue_state(IssueState& issue_state);
  void add_order_to_book(IssueState& issue_state, const Order& order);
  void remove_order_from_book(IssueState& issue_state, const Order& order, Volume quantity);
  IssueState& issue_state_for(const FlexPacketHeader& header);

  std::unordered_map<std::string, IssueState> issues_;
  ReplayStats stats_;
};

}  // namespace tse_mbo
