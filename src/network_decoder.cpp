#include "tse_mbo/network_decoder.hpp"

#include <bit>

namespace tse_mbo {

std::optional<UdpDatagramView> NetworkDecoder::decode_udp(const CaptureRecord& record) const {
  constexpr std::size_t kEthernetHeaderSize = 14;
  constexpr std::uint16_t kEtherTypeIpv4 = 0x0800;
  constexpr std::uint8_t kIpv4ProtocolUdp = 17;

  if (record.data.size() < kEthernetHeaderSize) {
    return std::nullopt;
  }

  const auto bytes = std::span<const std::byte>{record.data.data(), record.data.size()};
  const auto ether_type = read_be_u16(bytes.subspan(12, 2));
  if (ether_type != kEtherTypeIpv4) {
    return std::nullopt;
  }

  const auto ip_bytes = bytes.subspan(kEthernetHeaderSize);
  if (ip_bytes.size() < 20) {
    return std::nullopt;
  }

  const std::uint8_t version_ihl = std::to_integer<std::uint8_t>(ip_bytes[0]);
  const std::uint8_t version = static_cast<std::uint8_t>(version_ihl >> 4U);
  const std::uint8_t ihl_words = static_cast<std::uint8_t>(version_ihl & 0x0FU);
  if (version != 4 || ihl_words < 5) {
    return std::nullopt;
  }

  const std::size_t ipv4_header_size = static_cast<std::size_t>(ihl_words) * 4U;
  if (ip_bytes.size() < ipv4_header_size) {
    return std::nullopt;
  }

  const std::uint8_t protocol = std::to_integer<std::uint8_t>(ip_bytes[9]);
  if (protocol != kIpv4ProtocolUdp) {
    return std::nullopt;
  }

  const std::uint16_t total_length = read_be_u16(ip_bytes.subspan(2, 2));
  if (total_length < ipv4_header_size + 8U || ip_bytes.size() < total_length) {
    return std::nullopt;
  }

  const auto udp_bytes = ip_bytes.subspan(ipv4_header_size, total_length - ipv4_header_size);
  if (udp_bytes.size() < 8) {
    return std::nullopt;
  }

  const std::uint16_t udp_length = read_be_u16(udp_bytes.subspan(4, 2));
  if (udp_length < 8U || udp_length > udp_bytes.size()) {
    return std::nullopt;
  }

  UdpDatagramView datagram;
  datagram.capture_ts_sec = record.ts_sec;
  datagram.capture_ts_subsec = record.ts_subsec;
  datagram.src_ipv4 = read_be_u32(ip_bytes.subspan(12, 4));
  datagram.dst_ipv4 = read_be_u32(ip_bytes.subspan(16, 4));
  datagram.src_port = read_be_u16(udp_bytes.subspan(0, 2));
  datagram.dst_port = read_be_u16(udp_bytes.subspan(2, 2));

  const auto payload = udp_bytes.subspan(8, udp_length - 8U);
  datagram.payload.assign(payload.begin(), payload.end());
  return datagram;
}

std::uint16_t NetworkDecoder::read_be_u16(std::span<const std::byte> bytes) {
  return static_cast<std::uint16_t>((std::to_integer<std::uint8_t>(bytes[0]) << 8U) |
                                    std::to_integer<std::uint8_t>(bytes[1]));
}

std::uint32_t NetworkDecoder::read_be_u32(std::span<const std::byte> bytes) {
  return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 8U) |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3]));
}

}  // namespace tse_mbo

