#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ingest/pcap_reader.hpp"

namespace tse_mbo {

struct UdpDatagramView {
  std::uint32_t capture_ts_sec = 0;
  std::uint32_t capture_ts_subsec = 0;
  std::uint32_t src_ipv4 = 0;
  std::uint32_t dst_ipv4 = 0;
  std::uint16_t src_port = 0;
  std::uint16_t dst_port = 0;
  std::vector<std::byte> payload;
};

class NetworkDecoder {
 public:
  std::optional<UdpDatagramView> decode_udp(const CaptureRecord& record) const;

 private:
  static std::uint16_t read_be_u16(std::span<const std::byte> bytes);
  static std::uint32_t read_be_u32(std::span<const std::byte> bytes);
};

}  // namespace tse_mbo

