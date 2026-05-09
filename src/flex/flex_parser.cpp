#include "flex/flex_parser.hpp"

#include <bit>

namespace tse_mbo {

std::vector<FlexPacketView> FlexParser::parse_all(const UdpDatagramView& datagram) const {
  constexpr std::size_t kHeaderLength = 26;
  std::vector<FlexPacketView> packets;
  if (datagram.payload.size() < kHeaderLength) {
    return packets;
  }

  const auto bytes = std::span<const std::byte>{datagram.payload.data(), datagram.payload.size()};
  std::size_t packet_offset = 0;
  while (packet_offset + kHeaderLength <= bytes.size()) {
    FlexPacketView packet;
    packet.datagram = datagram;

    const auto packet_bytes = bytes.subspan(packet_offset);
    packet.header.multicast_group_number = std::to_integer<std::uint8_t>(packet_bytes[0]);
    packet.header.system_reboots = std::to_integer<std::uint8_t>(packet_bytes[1]);
    packet.header.sequence_number = read_be_u32(packet_bytes.subspan(2, 4));
    packet.header.issue_code = read_ascii_trimmed(packet_bytes.subspan(6, 12));
    packet.header.update_number = read_be_u32(packet_bytes.subspan(18, 4));
    packet.header.packet_number = std::to_integer<std::uint8_t>(packet_bytes[22]);
    packet.header.total_number_of_packets = std::to_integer<std::uint8_t>(packet_bytes[23]);
    packet.header.utility_flag = std::to_integer<std::uint8_t>(packet_bytes[24]);
    packet.header.message_count = std::to_integer<std::uint8_t>(packet_bytes[25]);

    std::size_t offset = packet_offset + kHeaderLength;
    bool ok = true;
    for (std::uint8_t tag_index = 0; tag_index < packet.header.message_count; ++tag_index) {
      if (offset >= bytes.size()) {
        ok = false;
        break;
      }
      const std::uint8_t tag_length = std::to_integer<std::uint8_t>(bytes[offset]);
      ++offset;

      if (tag_length == 0 || offset + tag_length > bytes.size()) {
        ok = false;
        break;
      }

      FlexTagView tag;
      tag.length = tag_length;
      tag.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + tag_length));
      if (!tag.bytes.empty()) {
        tag.message_type = static_cast<char>(std::to_integer<std::uint8_t>(tag.bytes.front()));
      }
      packet.tags.push_back(std::move(tag));
      offset += tag_length;
    }

    if (!ok || packet.tags.size() != packet.header.message_count) {
      break;
    }

    packets.push_back(std::move(packet));
    packet_offset = offset;
  }

  return packets;
}

std::uint32_t FlexParser::read_be_u32(std::span<const std::byte> bytes) {
  return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8U) |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
}

std::string FlexParser::read_ascii_trimmed(std::span<const std::byte> bytes) {
  std::string text;
  text.reserve(bytes.size());
  for (const auto b : bytes) {
    text.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
  }

  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
  return text;
}

}  // namespace tse_mbo
