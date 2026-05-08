#pragma once

#include <cstdint>
#include <string>

namespace tse_flex_mbo {

using Price = std::uint64_t;

constexpr std::uint32_t kPriceScale = 10000U;

enum class Side : char {
  buy = 'B',
  sell = 'S',
};

enum class TagType : char {
  seconds_timestamp = 'T',
  trading_status = 'O',
  execution_summary = 'K',
  add_order = 'A',
  order_executed = 'E',
  order_executed_with_price = 'C',
  order_delete = 'D',
  reset = 'R',
  communication_control = 'L',
  unknown = '?',
};

struct PacketHeader {
  std::uint8_t multicast_group_number = 0;
  std::uint8_t system_reboots = 0;
  std::uint32_t sequence_number = 0;
  std::string issue_code;
  std::uint32_t update_number = 0;
  std::uint8_t packet_number = 0;
  std::uint8_t total_number_of_packets = 0;
  std::uint8_t utility_flag = 0;
  std::uint8_t message_count = 0;
};

struct Order {
  std::uint32_t order_id = 0;
  std::string issue_code;
  Side side = Side::buy;
  std::uint64_t quantity = 0;
  Price price = 0;
  std::uint8_t order_condition = 0;
  bool preserves_time_priority = false;
};

inline std::string to_string(Side side) {
  return side == Side::buy ? "buy" : "sell";
}

inline std::string to_string(TagType tag_type) {
  switch (tag_type) {
    case TagType::seconds_timestamp:
      return "T";
    case TagType::trading_status:
      return "O";
    case TagType::execution_summary:
      return "K";
    case TagType::add_order:
      return "A";
    case TagType::order_executed:
      return "E";
    case TagType::order_executed_with_price:
      return "C";
    case TagType::order_delete:
      return "D";
    case TagType::reset:
      return "R";
    case TagType::communication_control:
      return "L";
    case TagType::unknown:
      return "?";
  }
  return "?";
}

}  // namespace tse_flex_mbo

