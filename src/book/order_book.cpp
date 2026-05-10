#include "book/order_book.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <variant>

#include "book/indicative.hpp"

namespace tse_mbo {

namespace {

bool is_opening_eligible_order_condition(std::uint8_t order_condition) noexcept {
  return order_condition == 0U || order_condition == 2U;
}

}  // namespace

void OrderBookReplayer::apply(const NormalizedFlexPacket& packet) {
  ++stats_.packets_seen;
  ++stats_.packets_parsed;

  IssueState& issue_state = issue_state_for(packet.header);
  issue_state.last_sequence_number = packet.header.sequence_number;
  issue_state.last_update_number = packet.header.update_number;
  issue_state.seen_tag_count += packet.tag_count;

  stats_.tags_seen += packet.tag_count;
  for (const auto& message : packet.messages) {
    apply_message(issue_state, message);
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
                                               Volume quantity) {
  if (!is_opening_eligible(order) || quantity == 0) {
    return;
  }

  const Volume amount = std::min(quantity, order.quantity);
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

void OrderBookReplayer::apply_message(IssueState& issue_state, const FlexMessage& message) {
  std::visit(
      [&](const auto& event) {
        using Event = std::decay_t<decltype(event)>;

        if constexpr (std::is_same_v<Event, AddOrderMessage>) {
          ++stats_.add_tags;
          Order order;
          order.order_id = event.order_id;
          order.side = event.side == 'B' ? Side::buy : event.side == 'S' ? Side::sell : Side::unknown;
          order.quantity = event.quantity;
          order.price = static_cast<Price>(event.price);
          order.order_condition = event.order_condition;
          order.modification_flag = event.modification_flag;

          const auto existing = issue_state.live_orders.find(order.order_id);
          if (existing != issue_state.live_orders.end()) {
            remove_order_from_book(issue_state, existing->second, existing->second.quantity);
          }

          issue_state.live_orders.insert_or_assign(order.order_id, order);
          add_order_to_book(issue_state, order);
          recalculate_issue_state(issue_state);
          return;
        }

        if constexpr (std::is_same_v<Event, DeleteOrderMessage>) {
          ++stats_.delete_tags;
          const auto it = issue_state.live_orders.find(event.order_id);
          if (it == issue_state.live_orders.end()) {
            return;
          }

          remove_order_from_book(issue_state, it->second, it->second.quantity);
          issue_state.live_orders.erase(it);
          recalculate_issue_state(issue_state);
          return;
        }

        if constexpr (std::is_same_v<Event, ExecuteOrderMessage>) {
          ++stats_.executed_tags;
          const auto it = issue_state.live_orders.find(event.order_id);
          if (it == issue_state.live_orders.end()) {
            return;
          }

          remove_order_from_book(issue_state, it->second, event.quantity);
          if (event.quantity >= it->second.quantity) {
            issue_state.live_orders.erase(it);
          } else {
            it->second.quantity -= event.quantity;
          }
          recalculate_issue_state(issue_state);
          return;
        }

        if constexpr (std::is_same_v<Event, ExecuteWithPriceOrderMessage>) {
          ++stats_.executed_with_price_tags;
          const auto it = issue_state.live_orders.find(event.order_id);
          if (it == issue_state.live_orders.end()) {
            return;
          }

          remove_order_from_book(issue_state, it->second, event.quantity);
          if (event.quantity >= it->second.quantity) {
            issue_state.live_orders.erase(it);
          } else {
            it->second.quantity -= event.quantity;
          }
          recalculate_issue_state(issue_state);
          return;
        }

        if constexpr (std::is_same_v<Event, ResetMessage>) {
          ++stats_.reset_tags;
          for (auto& [_, state] : issues_) {
            state.live_orders.clear();
            state.limit_price_levels.clear();
            state.market_bid_volume = 0;
            state.market_ask_volume = 0;
            state.previous_reference_price = state.base_price;
            state.last_indicative_match = {};
          }
          return;
        }
      },
      message);
}

IssueState& OrderBookReplayer::issue_state_for(const FlexPacketHeader& header) {
  auto key = header.issue_code;
  if (key.empty()) {
    key = "<control>";
  }
  auto [it, inserted] = issues_.try_emplace(key);
  if (inserted) {
    it->second.issue_code = key;
    if (const auto pending = pending_base_prices_.find(key); pending != pending_base_prices_.end()) {
      it->second.base_price = pending->second;
      it->second.previous_reference_price = pending->second;
    }
  }
  return it->second;
}

void OrderBookReplayer::set_base_price(const std::string& issue_code, Price base_price) {
  if (const auto existing = issues_.find(issue_code); existing != issues_.end()) {
    existing->second.base_price = base_price;
    if (!existing->second.previous_reference_price.has_value()) {
      existing->second.previous_reference_price = base_price;
    }
    return;
  }
  pending_base_prices_[issue_code] = base_price;
}

}  // namespace tse_mbo
