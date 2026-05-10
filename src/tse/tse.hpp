#pragma once

#include <string>
#include <unordered_map>

#include "book/order_book.hpp"
#include "replay/replay_runner.hpp"

namespace tse_mbo {

class Tse final : public ReplayDataCallback {
 public:
  void on_flex_packet(const NormalizedFlexPacket& normalized_packet) override;

  void set_base_price(const std::string& issue_code, Price base_price);

  const std::unordered_map<std::string, IssueState>& issues() const noexcept;
  const ReplayStats& stats() const noexcept;

 private:
  OrderBookReplayer order_book_;
};

}  // namespace tse_mbo
