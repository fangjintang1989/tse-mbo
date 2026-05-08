#pragma once

#include <cstdint>

#include "tse_mbo/order_book.hpp"

namespace tse_mbo {

struct IndicativeMatchResult {
  bool has_result = false;
  std::uint64_t price = 0;
  std::uint64_t volume = 0;
};

IndicativeMatchResult calculate_indicative_match(const IssueState& issue_state);

}  // namespace tse_mbo

