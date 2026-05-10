#include "flex/flex_message.hpp"

#include <bit>
#include <limits>
#include <span>

namespace tse_mbo {

namespace {

std::uint32_t read_be_u32(std::span<const std::byte> bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 8U) |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3]));
}

std::uint64_t read_be_u48(std::span<const std::byte> bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 40U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 32U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 24U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 16U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 4])) << 8U) |
         static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 5]));
}

std::uint64_t read_be_u64(std::span<const std::byte> bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 56U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 48U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 40U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 32U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 4])) << 24U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 5])) << 16U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 6])) << 8U) |
         static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 7]));
}

char parse_side(std::byte value) {
  const char side = static_cast<char>(std::to_integer<unsigned char>(value));
  if (side == 'B' || side == 'S') {
    return side;
  }
  return '?';
}

std::int64_t decode_price(std::uint64_t raw_price) {
  if (raw_price == std::numeric_limits<std::uint64_t>::max()) {
    return kNormalizedMarketOrderPrice;
  }
  if (raw_price > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::numeric_limits<std::int64_t>::max();
  }
  return static_cast<std::int64_t>(raw_price);
}

}  // namespace

NormalizedFlexPacket normalize_flex_packet(const FlexPacketView& packet) {
  NormalizedFlexPacket normalized;
  normalized.header = packet.header;
  normalized.tag_count = packet.tags.size();
  normalized.messages.reserve(packet.tags.size());

  for (const auto& tag : packet.tags) {
    if (tag.bytes.empty()) {
      continue;
    }

    const auto message_type = tag.message_type;
    const auto bytes = std::span<const std::byte>{tag.bytes.data(), tag.bytes.size()};

    switch (message_type) {
      case 'A':
        if (bytes.size() < 26U) {
          continue;
        }
        normalized.messages.push_back(AddOrderMessage{
            .order_id = read_be_u32(bytes, 5),
            .side = parse_side(bytes[9]),
            .quantity = read_be_u48(bytes, 10),
            .price = decode_price(read_be_u64(bytes, 16)),
            .order_condition = std::to_integer<std::uint8_t>(bytes[24]),
            .modification_flag = std::to_integer<std::uint8_t>(bytes[25]),
        });
        break;
      case 'D':
        if (bytes.size() < 11U) {
          continue;
        }
        normalized.messages.push_back(DeleteOrderMessage{.order_id = read_be_u32(bytes, 5)});
        break;
      case 'E':
        if (bytes.size() < 20U) {
          continue;
        }
        normalized.messages.push_back(
            ExecuteOrderMessage{.order_id = read_be_u32(bytes, 5), .quantity = read_be_u48(bytes, 10)});
        break;
      case 'C':
        if (bytes.size() < 20U) {
          continue;
        }
        normalized.messages.push_back(ExecuteWithPriceOrderMessage{
            .order_id = read_be_u32(bytes, 5), .quantity = read_be_u48(bytes, 10)});
        break;
      case 'R':
        normalized.messages.push_back(ResetMessage{});
        break;
      default:
        continue;
    }
  }

  return normalized;
}

}  // namespace tse_mbo
