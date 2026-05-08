#include "tse_flex_mbo/order_book.hpp"

#include <stdexcept>
#include <utility>

namespace tse_flex_mbo {

OrderBook::OrderBook(std::string issue_code) : issue_code_(std::move(issue_code)) {}

const std::string& OrderBook::issue_code() const noexcept {
  return issue_code_;
}

bool OrderBook::add_order(const Order& order) {
  if (order.issue_code != issue_code_) {
    throw std::invalid_argument("order issue code does not match order book issue code");
  }

  const auto [iterator, inserted] = orders_.insert_or_assign(order.order_id, order);
  (void)iterator;
  return inserted;
}

bool OrderBook::cancel_order(std::uint32_t order_id) {
  return orders_.erase(order_id) > 0;
}

bool OrderBook::execute_order(std::uint32_t order_id, std::uint64_t volume) {
  const auto it = orders_.find(order_id);
  if (it == orders_.end()) {
    return false;
  }

  if (volume >= it->second.quantity) {
    orders_.erase(it);
    return true;
  }

  it->second.quantity -= volume;
  return true;
}

void OrderBook::clear() {
  orders_.clear();
}

std::size_t OrderBook::order_count() const noexcept {
  return orders_.size();
}

std::optional<Order> OrderBook::find_order(std::uint32_t order_id) const {
  const auto it = orders_.find(order_id);
  if (it == orders_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<Order> OrderBook::snapshot() const {
  std::vector<Order> orders;
  orders.reserve(orders_.size());
  for (const auto& [_, order] : orders_) {
    orders.push_back(order);
  }
  return orders;
}

}  // namespace tse_flex_mbo
