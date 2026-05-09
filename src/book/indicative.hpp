#pragma once

#include "book/order_book.hpp"

namespace tse_mbo {

IndicativeMatchResult calculate_indicative_match(const IssueState& issue_state);

}  // namespace tse_mbo
