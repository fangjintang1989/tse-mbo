#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "flex/flex_message.hpp"

namespace tse_mbo {

class ReplayDataCallback {
 public:
  virtual ~ReplayDataCallback() = default;
  virtual void on_flex_packet(const NormalizedFlexPacket& packet) = 0;
};

struct ReplaySummary {
  std::uint64_t capture_records = 0;
  std::uint64_t udp_datagrams = 0;
};

ReplaySummary replay_pcaps(const std::vector<std::filesystem::path>& pcap_paths,
                           ReplayDataCallback& callback);

}  // namespace tse_mbo
