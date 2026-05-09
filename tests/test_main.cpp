#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "book/order_book.hpp"
#include "book/indicative.hpp"
#include "flex/flex_parser.hpp"
#include "ingest/network_decoder.hpp"
#include "ingest/pcap_reader.hpp"

namespace {

using tse_mbo::CaptureRecord;
using tse_mbo::FlexPacketView;
using tse_mbo::FlexParser;
using tse_mbo::FlexTagView;
using tse_mbo::OrderBookReplayer;
using tse_mbo::Side;
using tse_mbo::UdpDatagramView;

std::byte as_byte(std::uint8_t value) {
  return static_cast<std::byte>(value);
}

void expect(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void expect_price(tse_mbo::Price actual, tse_mbo::Price expected, std::string_view message) {
  if (std::fabs(actual - expected) > 0.00001) {
    throw std::runtime_error(std::string(message));
  }
}

void append_le_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(as_byte(static_cast<std::uint8_t>(value & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU)));
}

void append_le_u32(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(as_byte(static_cast<std::uint8_t>(value & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 16U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 24U) & 0xffU)));
}

void append_be_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>(value & 0xffU)));
}

void append_be_u32(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 24U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 16U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>(value & 0xffU)));
}

void append_be_u48(std::vector<std::byte>& out, std::uint64_t value) {
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 40U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 32U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 24U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 16U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>(value & 0xffU)));
}

void append_be_u64(std::vector<std::byte>& out, std::uint64_t value) {
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 56U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 48U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 40U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 32U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 24U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 16U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU)));
  out.push_back(as_byte(static_cast<std::uint8_t>(value & 0xffU)));
}

void append_ascii_padded(std::vector<std::byte>& out, const std::string& text, std::size_t width) {
  for (std::size_t index = 0; index < width; ++index) {
    const char ch = index < text.size() ? text[index] : ' ';
    out.push_back(as_byte(static_cast<std::uint8_t>(ch)));
  }
}

std::vector<std::byte> make_add_tag(std::uint32_t order_id,
                                    char side,
                                    std::uint64_t quantity,
                                    std::uint64_t raw_price,
                                    std::uint8_t order_condition = 0,
                                    std::uint8_t modification_flag = 0) {
  std::vector<std::byte> tag;
  tag.push_back(as_byte('A'));
  append_be_u32(tag, 0);
  append_be_u32(tag, order_id);
  tag.push_back(as_byte(static_cast<std::uint8_t>(side)));
  append_be_u48(tag, quantity);
  append_be_u64(tag, raw_price);
  tag.push_back(as_byte(order_condition));
  tag.push_back(as_byte(modification_flag));
  return tag;
}

std::vector<std::byte> make_delete_tag(std::uint32_t order_id) {
  std::vector<std::byte> tag;
  tag.push_back(as_byte('D'));
  append_be_u32(tag, 0);
  append_be_u32(tag, order_id);
  append_be_u16(tag, 0);
  return tag;
}

std::vector<std::byte> make_execute_tag(char type, std::uint32_t order_id, std::uint64_t quantity) {
  std::vector<std::byte> tag;
  tag.push_back(as_byte(static_cast<std::uint8_t>(type)));
  append_be_u32(tag, 0);
  append_be_u32(tag, order_id);
  tag.push_back(as_byte(0));
  append_be_u48(tag, quantity);

  const std::size_t target_size = type == 'C' ? 29U : 20U;
  while (tag.size() < target_size) {
    tag.push_back(as_byte(0));
  }
  return tag;
}

std::vector<std::byte> make_reset_tag() {
  return {as_byte('R')};
}

std::vector<std::byte> make_flex_packet(const std::string& issue_code,
                                        std::uint32_t sequence_number,
                                        std::uint32_t update_number,
                                        const std::vector<std::vector<std::byte>>& tags,
                                        std::uint8_t packet_number = 1,
                                        std::uint8_t total_packets = 1,
                                        std::uint8_t utility_flag = 0) {
  std::vector<std::byte> packet;
  packet.push_back(as_byte(1));
  packet.push_back(as_byte(0));
  append_be_u32(packet, sequence_number);
  append_ascii_padded(packet, issue_code, 12);
  append_be_u32(packet, update_number);
  packet.push_back(as_byte(packet_number));
  packet.push_back(as_byte(total_packets));
  packet.push_back(as_byte(utility_flag));
  packet.push_back(as_byte(static_cast<std::uint8_t>(tags.size())));

  for (const auto& tag : tags) {
    packet.push_back(as_byte(static_cast<std::uint8_t>(tag.size())));
    packet.insert(packet.end(), tag.begin(), tag.end());
  }

  return packet;
}

CaptureRecord make_udp_capture_record(const std::vector<std::byte>& payload) {
  std::vector<std::byte> frame;

  for (int i = 0; i < 12; ++i) {
    frame.push_back(as_byte(static_cast<std::uint8_t>(i)));
  }
  append_be_u16(frame, 0x0800);

  const std::uint16_t ipv4_total_length = static_cast<std::uint16_t>(20U + 8U + payload.size());
  frame.push_back(as_byte(0x45));
  frame.push_back(as_byte(0));
  append_be_u16(frame, ipv4_total_length);
  append_be_u16(frame, 0);
  append_be_u16(frame, 0);
  frame.push_back(as_byte(64));
  frame.push_back(as_byte(17));
  append_be_u16(frame, 0);
  append_be_u32(frame, 0x0a000001U);
  append_be_u32(frame, 0x0a000002U);

  const std::uint16_t udp_length = static_cast<std::uint16_t>(8U + payload.size());
  append_be_u16(frame, 15000);
  append_be_u16(frame, 16000);
  append_be_u16(frame, udp_length);
  append_be_u16(frame, 0);

  frame.insert(frame.end(), payload.begin(), payload.end());

  CaptureRecord record;
  record.ts_sec = 1234;
  record.ts_subsec = 5678;
  record.data = std::move(frame);
  return record;
}

std::vector<std::byte> make_classic_pcap_bytes(const CaptureRecord& record) {
  std::vector<std::byte> bytes;
  append_le_u32(bytes, 0xa1b2c3d4U);
  append_le_u16(bytes, 2);
  append_le_u16(bytes, 4);
  append_le_u32(bytes, 0);
  append_le_u32(bytes, 0);
  append_le_u32(bytes, 65535);
  append_le_u32(bytes, 1);

  append_le_u32(bytes, record.ts_sec);
  append_le_u32(bytes, record.ts_subsec);
  append_le_u32(bytes, static_cast<std::uint32_t>(record.data.size()));
  append_le_u32(bytes, static_cast<std::uint32_t>(record.data.size()));
  bytes.insert(bytes.end(), record.data.begin(), record.data.end());
  return bytes;
}

void write_gzip_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  gzFile file = gzopen(path.string().c_str(), "wb");
  if (file == nullptr) {
    throw std::runtime_error("Failed to create gzip file for test");
  }

  const int written =
      gzwrite(file, reinterpret_cast<const void*>(bytes.data()), static_cast<unsigned int>(bytes.size()));
  if (written != static_cast<int>(bytes.size())) {
    gzclose(file);
    throw std::runtime_error("Failed to write gzip file for test");
  }

  gzclose(file);
}

void test_pcap_reader_reads_gzipped_classic_pcap() {
  const auto payload = make_flex_packet("TEST1", 42, 7, {make_reset_tag()});
  const auto record = make_udp_capture_record(payload);
  const auto pcap_bytes = make_classic_pcap_bytes(record);

  const auto temp_path =
      std::filesystem::temp_directory_path() / "tse_mbo_pcap_reader_test.pcap.gz";
  write_gzip_file(temp_path, pcap_bytes);

  const tse_mbo::PcapReader reader;
  const auto records = reader.read_all(temp_path);

  expect(records.size() == 1, "PcapReader should return one record");
  expect(records.front().ts_sec == 1234, "Capture ts_sec should round-trip");
  expect(records.front().ts_subsec == 5678, "Capture ts_subsec should round-trip");
  expect(records.front().data == record.data, "Capture frame bytes should round-trip");

  std::filesystem::remove(temp_path);
}

void test_network_decoder_extracts_udp_payload() {
  const auto payload = make_flex_packet("TEST2", 43, 8, {make_reset_tag()});
  const auto record = make_udp_capture_record(payload);

  const tse_mbo::NetworkDecoder decoder;
  const auto datagram = decoder.decode_udp(record);

  expect(datagram.has_value(), "NetworkDecoder should decode a valid UDP frame");
  expect(datagram->capture_ts_sec == 1234, "Datagram should keep capture timestamp");
  expect(datagram->src_ipv4 == 0x0a000001U, "Source IPv4 should be decoded");
  expect(datagram->dst_ipv4 == 0x0a000002U, "Destination IPv4 should be decoded");
  expect(datagram->src_port == 15000, "Source UDP port should be decoded");
  expect(datagram->dst_port == 16000, "Destination UDP port should be decoded");
  expect(datagram->payload == payload, "UDP payload should be extracted exactly");
}

void test_flex_parser_handles_multiple_packets_in_one_datagram() {
  const auto packet_one = make_flex_packet("7203", 100, 11, {make_add_tag(101, 'B', 500, 123400)});
  const auto packet_two = make_flex_packet("7203", 101, 12, {make_delete_tag(101)});

  UdpDatagramView datagram;
  datagram.payload = packet_one;
  datagram.payload.insert(datagram.payload.end(), packet_two.begin(), packet_two.end());

  const FlexParser parser;
  const auto packets = parser.parse_all(datagram);

  expect(packets.size() == 2, "FlexParser should split two FLEX packets from one UDP payload");
  expect(packets[0].header.sequence_number == 100, "First packet sequence number mismatch");
  expect(packets[1].header.sequence_number == 101, "Second packet sequence number mismatch");
  expect(packets[0].header.issue_code == "7203", "Issue code should be ASCII-trimmed");
  expect(packets[0].tags.size() == 1, "First packet should expose one tag");
  expect(packets[1].tags.size() == 1, "Second packet should expose one tag");
  expect(packets[0].tags[0].message_type == 'A', "First tag should be A");
  expect(packets[1].tags[0].message_type == 'D', "Second tag should be D");
}

void test_order_book_replayer_replays_add_execute_delete_and_reset() {
  OrderBookReplayer replayer;

  FlexPacketView packet_one;
  packet_one.header.issue_code = "7203";
  packet_one.header.sequence_number = 1000;
  packet_one.header.update_number = 10;
  packet_one.tags = {FlexTagView{'A', 26, make_add_tag(111, 'B', 1000, 200000)},
                     FlexTagView{'A', 26, make_add_tag(222, 'S', 700, 200500)}};

  FlexPacketView packet_two;
  packet_two.header.issue_code = "7203";
  packet_two.header.sequence_number = 1001;
  packet_two.header.update_number = 11;
  packet_two.tags = {FlexTagView{'E', 20, make_execute_tag('E', 111, 400)},
                     FlexTagView{'C', 29, make_execute_tag('C', 222, 700)}};

  FlexPacketView packet_three;
  packet_three.header.issue_code = "7203";
  packet_three.header.sequence_number = 1002;
  packet_three.header.update_number = 12;
  packet_three.tags = {FlexTagView{'D', 11, make_delete_tag(111)}};

  FlexPacketView packet_four;
  packet_four.header.issue_code = "8306";
  packet_four.header.sequence_number = 2000;
  packet_four.header.update_number = 20;
  packet_four.tags = {FlexTagView{'A', 26, make_add_tag(333, 'S', 900, 301000)}};

  FlexPacketView packet_five;
  packet_five.header.issue_code = "";
  packet_five.header.sequence_number = 3000;
  packet_five.header.update_number = 30;
  packet_five.tags = {FlexTagView{'R', 1, make_reset_tag()}};

  replayer.apply(packet_one);
  const auto& issue_7203_after_add = replayer.issues().at("7203");
  expect_price(issue_7203_after_add.live_orders.at(222).price, 20.05,
               "Raw add price with fractional digits should decode into real order price");
  expect(issue_7203_after_add.limit_price_levels.at(20.05).ask_volume == 700,
         "Book should use real decimal prices as ladder keys");

  replayer.apply(packet_two);

  const auto& issue_7203_after_exec = replayer.issues().at("7203");
  expect(issue_7203_after_exec.live_orders.size() == 1, "One order should remain after execution");
  expect(issue_7203_after_exec.live_orders.contains(111), "Order 111 should remain live");
  expect(issue_7203_after_exec.live_orders.at(111).quantity == 600, "Partial execution should reduce quantity");
  expect_price(issue_7203_after_exec.live_orders.at(111).price, 20.0,
               "Raw add price should be decoded into real order price");
  expect(issue_7203_after_exec.live_orders.at(111).side == Side::buy, "Buy side should be preserved");
  expect(issue_7203_after_exec.limit_price_levels.find(20.05) == issue_7203_after_exec.limit_price_levels.end(),
         "Fully executed sell order should remove the real-price level");

  replayer.apply(packet_three);
  expect(replayer.issues().at("7203").live_orders.empty(), "Delete should remove the remaining live order");

  replayer.apply(packet_four);
  expect(replayer.issues().at("8306").live_orders.size() == 1, "Second issue should track its own live order");

  replayer.apply(packet_five);
  expect(replayer.issues().at("7203").live_orders.empty(), "Reset should clear issue 7203");
  expect(replayer.issues().at("8306").live_orders.empty(), "Reset should clear issue 8306");

  const auto& stats = replayer.stats();
  expect(stats.packets_parsed == 5, "Replay stats should count packets");
  expect(stats.tags_seen == 7, "Replay stats should count tags");
  expect(stats.add_tags == 3, "Replay stats should count add tags");
  expect(stats.delete_tags == 1, "Replay stats should count delete tags");
  expect(stats.executed_tags == 1, "Replay stats should count E tags");
  expect(stats.executed_with_price_tags == 1, "Replay stats should count C tags");
  expect(stats.reset_tags == 1, "Replay stats should count reset tags");
  expect(!replayer.issues().at("7203").previous_reference_price.has_value(),
         "Reset should clear the indicative reference price");
}

void test_order_book_replayer_tracks_opening_eligible_orders_only() {
  OrderBookReplayer replayer;

  FlexPacketView packet;
  packet.header.issue_code = "7203";
  packet.header.sequence_number = 4000;
  packet.header.update_number = 40;
  packet.tags = {FlexTagView{'A', 26, make_add_tag(401, 'B', 500, 100000, 4)},
                 FlexTagView{'A', 26, make_add_tag(402, 'S', 250, 100000, 2)}};

  replayer.apply(packet);

  const auto& issue = replayer.issues().at("7203");
  expect(issue.live_orders.size() == 2, "Both orders should be tracked in live state");
  expect(issue.limit_price_levels.size() == 1, "Only opening-eligible orders should reach the book");
  expect(issue.limit_price_levels.at(10.0).bid_volume == 0, "On-close buy should be excluded");
  expect(issue.limit_price_levels.at(10.0).ask_volume == 250, "On-open sell should be included");
  expect(issue.last_indicative_match.has_result, "Single-sided book should still produce a boundary result");
  expect_price(issue.last_indicative_match.price, 10.0, "Boundary result should use the available price level");
  expect(issue.last_indicative_match.volume == 0, "Single-sided boundary result should have zero volume");
}

void test_order_book_replayer_replaces_existing_order_at_same_id() {
  OrderBookReplayer replayer;

  FlexPacketView initial_packet;
  initial_packet.header.issue_code = "7203";
  initial_packet.header.sequence_number = 5000;
  initial_packet.header.update_number = 50;
  initial_packet.tags = {FlexTagView{'A', 26, make_add_tag(501, 'B', 1000, 200000, 0)}};

  FlexPacketView replacement_packet;
  replacement_packet.header.issue_code = "7203";
  replacement_packet.header.sequence_number = 5001;
  replacement_packet.header.update_number = 51;
  replacement_packet.tags = {FlexTagView{'A', 26, make_add_tag(501, 'B', 700, 210000, 0)}};

  replayer.apply(initial_packet);
  replayer.apply(replacement_packet);

  const auto& issue = replayer.issues().at("7203");
  expect(issue.live_orders.size() == 1, "Replacement should keep one live order");
  expect(issue.live_orders.at(501).quantity == 700, "Replacement should update order quantity");
  expect_price(issue.live_orders.at(501).price, 21.0, "Replacement should update order price");
  expect(issue.limit_price_levels.size() == 1, "Replacement should leave one price level");
  expect(issue.limit_price_levels.at(21.0).bid_volume == 700, "New price level should be populated");
  expect(issue.limit_price_levels.find(20.0) == issue.limit_price_levels.end(),
         "Old price level should be removed");
}

void test_indicative_match_supports_market_orders() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "7203";
  issue_state.market_bid_volume = 15;
  issue_state.market_ask_volume = 15;
  issue_state.limit_price_levels[20.0].bid_volume = 5;
  issue_state.limit_price_levels[20.0].ask_volume = 5;

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "Market orders should still allow a match");
  expect_price(result.price, 20.0, "The single candidate price should be selected");
  expect(result.volume == 20, "Market orders should contribute to IEV");
}

void test_indicative_match_uses_direction_reversal_tie_break() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "7203";
  issue_state.previous_reference_price = 10.0;
  issue_state.limit_price_levels[10.0].bid_volume = 5;
  issue_state.limit_price_levels[20.0].ask_volume = 5;

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "Reversal search should still yield a result");
  expect_price(result.price, 10.0, "Tie reversal should keep the current cursor candidate");
  expect(result.volume == 0, "The candidate volume should be preserved");
}

}  // namespace

int main() {
  try {
    test_pcap_reader_reads_gzipped_classic_pcap();
    test_network_decoder_extracts_udp_payload();
    test_flex_parser_handles_multiple_packets_in_one_datagram();
    test_order_book_replayer_replays_add_execute_delete_and_reset();
    test_order_book_replayer_tracks_opening_eligible_orders_only();
    test_order_book_replayer_replaces_existing_order_at_same_id();
    test_indicative_match_supports_market_orders();
    test_indicative_match_uses_direction_reversal_tie_break();
  } catch (const std::exception& ex) {
    std::cerr << "Test failure: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "All tests passed.\n";
  return 0;
}
