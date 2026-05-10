#include "tse/tse.hpp"

namespace tse_mbo {

void Tse::on_flex_packet(const NormalizedFlexPacket& normalized_packet) {
  order_book_.apply(normalized_packet);
}

void Tse::set_base_price(const std::string& issue_code, Price base_price) {
  order_book_.set_base_price(issue_code, base_price);
}

const std::unordered_map<std::string, IssueState>& Tse::issues() const noexcept {
  return order_book_.issues();
}

const ReplayStats& Tse::stats() const noexcept {
  return order_book_.stats();
}

}  // namespace tse_mbo
