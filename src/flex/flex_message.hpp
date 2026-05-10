#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "flex/flex_parser.hpp"

namespace tse_mbo {

inline constexpr std::int64_t kNormalizedMarketOrderPrice = -1;

struct AddOrderMessage {
  std::uint32_t order_id = 0;
  char side = '?';
  std::uint64_t quantity = 0;
  std::int64_t price = 0;
  std::uint8_t order_condition = 0;
  std::uint8_t modification_flag = 0;
};

struct DeleteOrderMessage {
  std::uint32_t order_id = 0;
};

struct ExecuteOrderMessage {
  std::uint32_t order_id = 0;
  std::uint64_t quantity = 0;
};

struct ExecuteWithPriceOrderMessage {
  std::uint32_t order_id = 0;
  std::uint64_t quantity = 0;
};

struct ResetMessage {};

using FlexMessage = std::variant<AddOrderMessage,
                                 DeleteOrderMessage,
                                 ExecuteOrderMessage,
                                 ExecuteWithPriceOrderMessage,
                                 ResetMessage>;

struct NormalizedFlexPacket {
  FlexPacketHeader header;
  std::size_t tag_count = 0;
  std::vector<FlexMessage> messages;
};

NormalizedFlexPacket normalize_flex_packet(const FlexPacketView& packet);

}  // namespace tse_mbo
