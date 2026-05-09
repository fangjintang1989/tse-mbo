#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "flex/flex_parser.hpp"

namespace tse_mbo {

using Price = double;
using RawPrice = std::uint64_t;
inline constexpr Price kMarketOrderPrice = -1.0;
inline constexpr RawPrice kRawMarketOrderPrice = std::numeric_limits<RawPrice>::max();
inline constexpr double kPriceScale = 10000.0;

struct IndicativeMatchResult {
  bool has_result = false;
  Price price = 0.0;
  std::uint64_t volume = 0;
};

enum class Side : char {
  buy = 'B',
  sell = 'S',
  unknown = '?',
};

struct Order {
  std::uint32_t order_id = 0;
  Side side = Side::unknown;
  std::uint64_t quantity = 0;
  Price price = 0.0;
  std::uint8_t order_condition = 0;
  std::uint8_t modification_flag = 0;
};

struct PriceLevel {
  std::uint64_t bid_volume = 0;
  std::uint64_t ask_volume = 0;
};

struct IssueState {
  std::string issue_code;
  std::unordered_map<std::uint32_t, Order> live_orders;
  std::map<Price, PriceLevel> limit_price_levels;
  std::uint64_t market_bid_volume = 0;
  std::uint64_t market_ask_volume = 0;
  std::optional<Price> previous_reference_price;
  IndicativeMatchResult last_indicative_match;
  std::uint32_t last_sequence_number = 0;
  std::uint32_t last_update_number = 0;
  std::uint64_t seen_tag_count = 0;
};

struct ReplayStats {
  std::uint64_t packets_seen = 0;
  std::uint64_t packets_parsed = 0;
  std::uint64_t tags_seen = 0;
  std::uint64_t add_tags = 0;
  std::uint64_t delete_tags = 0;
  std::uint64_t executed_tags = 0;
  std::uint64_t executed_with_price_tags = 0;
  std::uint64_t reset_tags = 0;
};

class OrderBookReplayer {
 public:
  void apply(const FlexPacketView& packet);

  const std::unordered_map<std::string, IssueState>& issues() const noexcept;
  const ReplayStats& stats() const noexcept;

 private:
  void apply_tag(const FlexPacketHeader& header, const FlexTagView& tag);
  static bool is_market_price(Price price) noexcept;
  static bool is_opening_eligible(const Order& order) noexcept;
  void recalculate_issue_state(IssueState& issue_state);
  void add_order_to_book(IssueState& issue_state, const Order& order);
  void remove_order_from_book(IssueState& issue_state, const Order& order, std::uint64_t quantity);
  static Price decode_price(std::uint64_t raw_price) noexcept;
  static std::uint32_t read_be_u32(const std::vector<std::byte>& bytes, std::size_t offset);
  static std::uint64_t read_be_u48(const std::vector<std::byte>& bytes, std::size_t offset);
  static std::uint64_t read_be_u64(const std::vector<std::byte>& bytes, std::size_t offset);
  static Side parse_side(std::byte value);
  IssueState& issue_state_for(const FlexPacketHeader& header);

  std::unordered_map<std::string, IssueState> issues_;
  ReplayStats stats_;
};

}  // namespace tse_mbo
