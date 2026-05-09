#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "book/order_book.hpp"

namespace tse_mbo::audit {

struct IndicativeAuditLevel {
  Price price = 0.0;
  std::uint64_t bid_volume = 0;
  std::uint64_t ask_volume = 0;
  std::uint64_t cum_bid = 0;
  std::uint64_t cum_ask = 0;
  std::int64_t tip_up = 0;
  std::int64_t tip_down = 0;
  bool selected = false;
};

inline bool same_price(Price lhs, Price rhs) {
  return std::fabs(lhs - rhs) <= 0.00001;
}

inline std::vector<IndicativeAuditLevel> build_indicative_audit_levels(
    const IssueState& issue_state,
    const IndicativeMatchResult& result) {
  std::vector<IndicativeAuditLevel> levels;
  levels.reserve(issue_state.limit_price_levels.size());

  for (const auto& [price, level] : issue_state.limit_price_levels) {
    levels.push_back(IndicativeAuditLevel{
        .price = price,
        .bid_volume = level.bid_volume,
        .ask_volume = level.ask_volume,
    });
  }

  std::uint64_t running_ask = issue_state.market_ask_volume;
  for (auto& level : levels) {
    running_ask += level.ask_volume;
    level.cum_ask = running_ask;
  }

  std::uint64_t running_bid = issue_state.market_bid_volume;
  for (std::size_t index = levels.size(); index > 0; --index) {
    auto& level = levels[index - 1U];
    running_bid += level.bid_volume;
    level.cum_bid = running_bid;
  }

  for (auto& level : levels) {
    level.tip_up = static_cast<std::int64_t>(level.cum_ask) -
                   static_cast<std::int64_t>(level.cum_bid) +
                   static_cast<std::int64_t>(level.bid_volume);
    level.tip_down = static_cast<std::int64_t>(level.cum_bid) -
                     static_cast<std::int64_t>(level.cum_ask) +
                     static_cast<std::int64_t>(level.ask_volume);
    level.selected = result.has_result && same_price(level.price, result.price);
  }

  return levels;
}

inline void write_csv_escaped(std::ostream& out, std::string_view value) {
  const bool needs_quotes =
      value.find_first_of(",\"\n\r") != std::string_view::npos;
  if (!needs_quotes) {
    out << value;
    return;
  }

  out << '"';
  for (const char ch : value) {
    if (ch == '"') {
      out << "\"\"";
    } else {
      out << ch;
    }
  }
  out << '"';
}

inline void write_indicative_audit_csv(
    std::ostream& out,
    const std::vector<std::string>& issue_codes,
    const std::unordered_map<std::string, IssueState>& issues,
    const std::map<std::string, std::string>& issue_names) {
  out << "symbol,issue_name,selected,price,bid_price,bid_volume,ask_price,ask_volume,"
         "cum_bid,cum_ask,tip_up,tip_down,iap,iav,market_bid_volume,market_ask_volume,"
         "live_orders,last_sequence,last_update\n";

  for (const auto& issue_code : issue_codes) {
    const auto issue_it = issues.find(issue_code);
    if (issue_it == issues.end()) {
      continue;
    }

    const auto& issue_state = issue_it->second;
    const auto& result = issue_state.last_indicative_match;
    const auto name_it = issue_names.find(issue_code);
    const std::string_view issue_name =
        name_it == issue_names.end() ? std::string_view{} : std::string_view{name_it->second};
    const auto levels = build_indicative_audit_levels(issue_state, result);

    if (levels.empty()) {
      out << issue_code << ',';
      write_csv_escaped(out, issue_name);
      out << ",no,0.0000,0.0000,0,0.0000,0,0,0,0,0,";
      if (result.has_result) {
        out << std::fixed << std::setprecision(4) << result.price;
      } else {
        out << "0.0000";
      }
      out << ',' << (result.has_result ? result.volume : 0)
          << ',' << issue_state.market_bid_volume
          << ',' << issue_state.market_ask_volume
          << ',' << issue_state.live_orders.size()
          << ',' << issue_state.last_sequence_number
          << ',' << issue_state.last_update_number << '\n';
      continue;
    }

    for (const auto& level : levels) {
      out << issue_code << ',';
      write_csv_escaped(out, issue_name);
      out << ',' << (level.selected ? "yes" : "no") << ','
          << std::fixed << std::setprecision(4) << level.price << ','
          << std::fixed << std::setprecision(4) << level.price << ','
          << level.bid_volume << ','
          << std::fixed << std::setprecision(4) << level.price << ','
          << level.ask_volume << ','
          << level.cum_bid << ','
          << level.cum_ask << ','
          << level.tip_up << ','
          << level.tip_down << ',';
      if (result.has_result) {
        out << std::fixed << std::setprecision(4) << result.price;
      } else {
        out << "0.0000";
      }
      out << ',' << (result.has_result ? result.volume : 0)
          << ',' << issue_state.market_bid_volume
          << ',' << issue_state.market_ask_volume
          << ',' << issue_state.live_orders.size()
          << ',' << issue_state.last_sequence_number
          << ',' << issue_state.last_update_number << '\n';
    }
  }
}

}  // namespace tse_mbo::audit
