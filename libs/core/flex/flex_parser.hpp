#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ingest/network_decoder.hpp"

namespace tse_mbo {

struct FlexPacketHeader {
  std::uint8_t multicast_group_number = 0;
  std::uint8_t system_reboots = 0;
  std::uint32_t sequence_number = 0;
  std::string issue_code;
  std::uint32_t update_number = 0;
  std::uint8_t packet_number = 0;
  std::uint8_t total_number_of_packets = 0;
  std::uint8_t utility_flag = 0;
  std::uint8_t message_count = 0;
};

struct FlexTagView {
  char message_type = '?';
  std::uint8_t length = 0;
  std::vector<std::byte> bytes;
};

struct FlexPacketView {
  UdpDatagramView datagram;
  FlexPacketHeader header;
  std::vector<FlexTagView> tags;
};

class FlexParser {
 public:
  std::optional<FlexPacketView> parse(const UdpDatagramView& datagram) const;

 private:
  static std::uint32_t read_be_u32(std::span<const std::byte> bytes);
  static std::string read_ascii_trimmed(std::span<const std::byte> bytes);
};

}  // namespace tse_mbo

