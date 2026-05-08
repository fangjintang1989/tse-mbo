#include "book/order_book.hpp"

#include <bit>

namespace tse_mbo {

void OrderBookReplayer::apply(const FlexPacketView& packet) {
  ++stats_.packets_seen;
  ++stats_.packets_parsed;

  for (const auto& tag : packet.tags) {
    ++stats_.tags_seen;
    apply_tag(packet.header, tag);
  }
}

const std::unordered_map<std::string, IssueState>& OrderBookReplayer::issues() const noexcept {
  return issues_;
}

const ReplayStats& OrderBookReplayer::stats() const noexcept {
  return stats_;
}

void OrderBookReplayer::apply_tag(const FlexPacketHeader& header, const FlexTagView& tag) {
  if (tag.bytes.empty()) {
    return;
  }

  IssueState& issue_state = issue_state_for(header);
  issue_state.last_sequence_number = header.sequence_number;
  issue_state.last_update_number = header.update_number;
  ++issue_state.seen_tag_count;

  switch (tag.message_type) {
    case 'A': {
      ++stats_.add_tags;
      if (tag.bytes.size() < 26) {
        return;
      }
      Order order;
      order.order_id = read_be_u32(tag.bytes, 5);
      order.side = parse_side(tag.bytes[9]);
      order.quantity = read_be_u48(tag.bytes, 10);
      order.price = read_be_u64(tag.bytes, 16);
      order.order_condition = std::to_integer<std::uint8_t>(tag.bytes[24]);
      order.modification_flag = std::to_integer<std::uint8_t>(tag.bytes[25]);
      issue_state.live_orders.insert_or_assign(order.order_id, order);
      return;
    }
    case 'D': {
      ++stats_.delete_tags;
      if (tag.bytes.size() < 11) {
        return;
      }
      const std::uint32_t order_id = read_be_u32(tag.bytes, 5);
      issue_state.live_orders.erase(order_id);
      return;
    }
    case 'E': {
      ++stats_.executed_tags;
      if (tag.bytes.size() < 20) {
        return;
      }
      const std::uint32_t order_id = read_be_u32(tag.bytes, 5);
      const std::uint64_t volume = read_be_u48(tag.bytes, 10);
      const auto it = issue_state.live_orders.find(order_id);
      if (it == issue_state.live_orders.end()) {
        return;
      }
      if (volume >= it->second.quantity) {
        issue_state.live_orders.erase(it);
      } else {
        it->second.quantity -= volume;
      }
      return;
    }
    case 'C': {
      ++stats_.executed_with_price_tags;
      if (tag.bytes.size() < 29) {
        return;
      }
      const std::uint32_t order_id = read_be_u32(tag.bytes, 5);
      const std::uint64_t volume = read_be_u48(tag.bytes, 10);
      const auto it = issue_state.live_orders.find(order_id);
      if (it == issue_state.live_orders.end()) {
        return;
      }
      if (volume >= it->second.quantity) {
        issue_state.live_orders.erase(it);
      } else {
        it->second.quantity -= volume;
      }
      return;
    }
    case 'R': {
      ++stats_.reset_tags;
      for (auto& [_, state] : issues_) {
        state.live_orders.clear();
      }
      return;
    }
    default:
      return;
  }
}

std::uint32_t OrderBookReplayer::read_be_u32(const std::vector<std::byte>& bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 8U) |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3]));
}

std::uint64_t OrderBookReplayer::read_be_u48(const std::vector<std::byte>& bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 40U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 32U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 24U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 16U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 4])) << 8U) |
         static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 5]));
}

std::uint64_t OrderBookReplayer::read_be_u64(const std::vector<std::byte>& bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 56U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 48U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 40U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 32U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 4])) << 24U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 5])) << 16U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 6])) << 8U) |
         static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 7]));
}

Side OrderBookReplayer::parse_side(std::byte value) {
  const char side = static_cast<char>(std::to_integer<unsigned char>(value));
  if (side == 'B') {
    return Side::buy;
  }
  if (side == 'S') {
    return Side::sell;
  }
  return Side::unknown;
}

IssueState& OrderBookReplayer::issue_state_for(const FlexPacketHeader& header) {
  auto key = header.issue_code;
  if (key.empty()) {
    key = "<control>";
  }
  auto [it, inserted] = issues_.try_emplace(key);
  if (inserted) {
    it->second.issue_code = key;
  }
  return it->second;
}

}  // namespace tse_mbo
