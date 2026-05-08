#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tse_mbo/flex_parser.hpp"

namespace tse_mbo {

using Price = std::uint64_t;

enum class Side : char {
  buy = 'B',
  sell = 'S',
  unknown = '?',
};

struct Order {
  std::uint32_t order_id = 0;
  Side side = Side::unknown;
  std::uint64_t quantity = 0;
  Price price = 0;
  std::uint8_t order_condition = 0;
  std::uint8_t modification_flag = 0;
};

struct IssueState {
  std::string issue_code;
  std::unordered_map<std::uint32_t, Order> live_orders;
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
  static std::uint32_t read_be_u32(const std::vector<std::byte>& bytes, std::size_t offset);
  static std::uint64_t read_be_u48(const std::vector<std::byte>& bytes, std::size_t offset);
  static std::uint64_t read_be_u64(const std::vector<std::byte>& bytes, std::size_t offset);
  static Side parse_side(std::byte value);
  IssueState& issue_state_for(const FlexPacketHeader& header);

  std::unordered_map<std::string, IssueState> issues_;
  ReplayStats stats_;
};

}  // namespace tse_mbo

