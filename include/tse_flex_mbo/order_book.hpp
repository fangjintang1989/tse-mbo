#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tse_flex_mbo/market_data.hpp"

namespace tse_flex_mbo {

class OrderBook {
 public:
  explicit OrderBook(std::string issue_code);

  const std::string& issue_code() const noexcept;

  bool add_order(const Order& order);
  bool cancel_order(std::uint32_t order_id);
  bool execute_order(std::uint32_t order_id, std::uint64_t volume);
  void clear();

  [[nodiscard]] std::size_t order_count() const noexcept;
  [[nodiscard]] std::optional<Order> find_order(std::uint32_t order_id) const;
  [[nodiscard]] std::vector<Order> snapshot() const;

 private:
  std::string issue_code_;
  std::unordered_map<std::uint32_t, Order> orders_;
};

}  // namespace tse_flex_mbo

