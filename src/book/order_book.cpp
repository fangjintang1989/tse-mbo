#include "book/order_book.hpp"

#include <algorithm>
#include <bit>

#include "book/indicative.hpp"

namespace tse_mbo {

namespace {

bool is_opening_eligible_order_condition(std::uint8_t order_condition) noexcept {
  return order_condition == 0U || order_condition == 2U;
}

}  // namespace

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

bool OrderBookReplayer::is_market_price(Price price) noexcept {
  return price == kMarketOrderPrice;
}

bool OrderBookReplayer::is_opening_eligible(const Order& order) noexcept {
  return is_opening_eligible_order_condition(order.order_condition);
}

void OrderBookReplayer::recalculate_issue_state(IssueState& issue_state) {
  issue_state.last_indicative_match = calculate_indicative_match(issue_state);
  if (issue_state.last_indicative_match.has_result) {
    issue_state.previous_reference_price = issue_state.last_indicative_match.price;
  }
}

void OrderBookReplayer::add_order_to_book(IssueState& issue_state, const Order& order) {
  if (!is_opening_eligible(order) || order.quantity == 0) {
    return;
  }

  if (is_market_price(order.price)) {
    if (order.side == Side::buy) {
      issue_state.market_bid_volume += order.quantity;
    } else if (order.side == Side::sell) {
      issue_state.market_ask_volume += order.quantity;
    }
    return;
  }

  auto& level = issue_state.limit_price_levels[order.price];
  if (order.side == Side::buy) {
    level.bid_volume += order.quantity;
  } else if (order.side == Side::sell) {
    level.ask_volume += order.quantity;
  }
}

void OrderBookReplayer::remove_order_from_book(IssueState& issue_state,
                                               const Order& order,
                                               std::uint64_t quantity) {
  if (!is_opening_eligible(order) || quantity == 0) {
    return;
  }

  const std::uint64_t amount = std::min(quantity, order.quantity);
  if (amount == 0) {
    return;
  }

  if (is_market_price(order.price)) {
    if (order.side == Side::buy) {
      issue_state.market_bid_volume = issue_state.market_bid_volume > amount
                                          ? issue_state.market_bid_volume - amount
                                          : 0;
    } else if (order.side == Side::sell) {
      issue_state.market_ask_volume = issue_state.market_ask_volume > amount
                                          ? issue_state.market_ask_volume - amount
                                          : 0;
    }
    return;
  }

  const auto it = issue_state.limit_price_levels.find(order.price);
  if (it == issue_state.limit_price_levels.end()) {
    return;
  }

  auto& level = it->second;
  if (order.side == Side::buy) {
    level.bid_volume = level.bid_volume > amount ? level.bid_volume - amount : 0;
  } else if (order.side == Side::sell) {
    level.ask_volume = level.ask_volume > amount ? level.ask_volume - amount : 0;
  }

  if (level.bid_volume == 0 && level.ask_volume == 0) {
    issue_state.limit_price_levels.erase(it);
  }
}

Price OrderBookReplayer::decode_price(std::uint64_t raw_price) noexcept {
  if (raw_price == kRawMarketOrderPrice) {
    return kMarketOrderPrice;
  }
  return static_cast<Price>(raw_price) / kPriceScale;
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
      order.price = decode_price(read_be_u64(tag.bytes, 16));
      order.order_condition = std::to_integer<std::uint8_t>(tag.bytes[24]);
      order.modification_flag = std::to_integer<std::uint8_t>(tag.bytes[25]);

      const auto existing = issue_state.live_orders.find(order.order_id);
      if (existing != issue_state.live_orders.end()) {
        remove_order_from_book(issue_state, existing->second, existing->second.quantity);
      }

      issue_state.live_orders.insert_or_assign(order.order_id, order);
      add_order_to_book(issue_state, order);
      recalculate_issue_state(issue_state);
      return;
    }
    case 'D': {
      ++stats_.delete_tags;
      if (tag.bytes.size() < 11) {
        return;
      }

      const std::uint32_t order_id = read_be_u32(tag.bytes, 5);
      const auto it = issue_state.live_orders.find(order_id);
      if (it == issue_state.live_orders.end()) {
        return;
      }

      remove_order_from_book(issue_state, it->second, it->second.quantity);
      issue_state.live_orders.erase(it);
      recalculate_issue_state(issue_state);
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

      remove_order_from_book(issue_state, it->second, volume);
      if (volume >= it->second.quantity) {
        issue_state.live_orders.erase(it);
      } else {
        it->second.quantity -= volume;
      }
      recalculate_issue_state(issue_state);
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

      remove_order_from_book(issue_state, it->second, volume);
      if (volume >= it->second.quantity) {
        issue_state.live_orders.erase(it);
      } else {
        it->second.quantity -= volume;
      }
      recalculate_issue_state(issue_state);
      return;
    }
    case 'R': {
      ++stats_.reset_tags;
      for (auto& [_, state] : issues_) {
        state.live_orders.clear();
        state.limit_price_levels.clear();
        state.market_bid_volume = 0;
        state.market_ask_volume = 0;
        state.previous_reference_price.reset();
        state.last_indicative_match = {};
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
