#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "app/app.hpp"
#include "book/order_book.hpp"
#include "book/indicative.hpp"
#include "flex/flex_message.hpp"
#include "flex/flex_parser.hpp"
#include "ingest/network_decoder.hpp"
#include "ingest/pcap_reader.hpp"

namespace {

using tse_mbo::CaptureRecord;
using tse_mbo::AddOrderMessage;
using tse_mbo::DeleteOrderMessage;
using tse_mbo::ExecuteOrderMessage;
using tse_mbo::ExecuteWithPriceOrderMessage;
using tse_mbo::FlexParser;
using tse_mbo::FlexTagView;
using tse_mbo::FlexMessage;
using tse_mbo::NormalizedFlexPacket;
using tse_mbo::ResetMessage;
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
  if (actual != expected) {
    throw std::runtime_error(std::string(message));
  }
}

NormalizedFlexPacket make_packet(const std::string& issue_code,
                                 std::uint32_t sequence_number,
                                 std::uint32_t update_number,
                                 std::vector<FlexMessage> messages) {
  NormalizedFlexPacket packet;
  packet.header.issue_code = issue_code;
  packet.header.sequence_number = sequence_number;
  packet.header.update_number = update_number;
  packet.tag_count = messages.size();
  packet.messages = std::move(messages);
  return packet;
}

FlexMessage make_add_message(std::uint32_t order_id,
                             char side,
                             std::uint64_t quantity,
                             tse_mbo::Price price,
                             std::uint8_t order_condition = 0,
                             std::uint8_t modification_flag = 0) {
  return AddOrderMessage{
      .order_id = order_id,
      .side = side,
      .quantity = quantity,
      .price = price,
      .order_condition = order_condition,
      .modification_flag = modification_flag,
  };
}

FlexMessage make_delete_message(std::uint32_t order_id) {
  return DeleteOrderMessage{.order_id = order_id};
}

FlexMessage make_execute_message(std::uint32_t order_id, std::uint64_t quantity) {
  return ExecuteOrderMessage{.order_id = order_id, .quantity = quantity};
}

FlexMessage make_execute_with_price_message(std::uint32_t order_id, std::uint64_t quantity) {
  return ExecuteWithPriceOrderMessage{.order_id = order_id, .quantity = quantity};
}

FlexMessage make_reset_message() {
  return ResetMessage{};
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

CaptureRecord make_udp_capture_record(const std::vector<std::byte>& payload,
                                      std::uint32_t ts_sec = 1234,
                                      std::uint32_t ts_subsec = 5678) {
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
  record.ts_sec = ts_sec;
  record.ts_subsec = ts_subsec;
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

  const auto packet_one = make_packet(
      "7203", 1000, 10,
      {make_add_message(111, 'B', 1000, tse_mbo::make_price(20)),
       make_add_message(222, 'S', 700, tse_mbo::make_price(20, 500))});

  const auto packet_two = make_packet(
      "7203", 1001, 11,
      {make_execute_message(111, 400),
       make_execute_with_price_message(222, 700)});

  const auto packet_three = make_packet("7203", 1002, 12, {make_delete_message(111)});

  const auto packet_four = make_packet("8306", 2000, 20,
                                        {make_add_message(333, 'S', 900, tse_mbo::make_price(30, 1000))});

  const auto packet_five = make_packet("", 3000, 30, {make_reset_message()});

  replayer.apply(packet_one);
  const auto& issue_7203_after_add = replayer.issues().at("7203");
  expect_price(issue_7203_after_add.live_orders.at(222).price, tse_mbo::make_price(20, 500),
               "Raw add price with fractional digits should decode into real order price");
  expect(issue_7203_after_add.limit_price_levels.at(tse_mbo::make_price(20, 500)).ask_volume == 700,
         "Book should use real decimal prices as ladder keys");

  replayer.apply(packet_two);

  const auto& issue_7203_after_exec = replayer.issues().at("7203");
  expect(issue_7203_after_exec.live_orders.size() == 1, "One order should remain after execution");
  expect(issue_7203_after_exec.live_orders.contains(111), "Order 111 should remain live");
  expect(issue_7203_after_exec.live_orders.at(111).quantity == 600, "Partial execution should reduce quantity");
  expect_price(issue_7203_after_exec.live_orders.at(111).price, tse_mbo::make_price(20),
               "Raw add price should be decoded into real order price");
  expect(issue_7203_after_exec.live_orders.at(111).side == Side::buy, "Buy side should be preserved");
  expect(issue_7203_after_exec.limit_price_levels.find(tse_mbo::make_price(20, 500)) ==
             issue_7203_after_exec.limit_price_levels.end(),
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

  const auto packet = make_packet(
      "7203", 4000, 40,
      {make_add_message(401, 'B', 500, tse_mbo::make_price(10), 4),
       make_add_message(402, 'S', 250, tse_mbo::make_price(10), 2)});

  replayer.apply(packet);

  const auto& issue = replayer.issues().at("7203");
  expect(issue.live_orders.size() == 2, "Both orders should be tracked in live state");
  expect(issue.limit_price_levels.size() == 1, "Only opening-eligible orders should reach the book");
  expect(issue.limit_price_levels.at(tse_mbo::make_price(10)).bid_volume == 0,
         "On-close buy should be excluded");
  expect(issue.limit_price_levels.at(tse_mbo::make_price(10)).ask_volume == 250,
         "On-open sell should be included");
  expect(issue.last_indicative_match.has_result, "Single-sided book should still produce a boundary result");
  expect_price(issue.last_indicative_match.price, tse_mbo::make_price(10),
               "Boundary result should use the available price level");
  expect(issue.last_indicative_match.volume == 0, "Single-sided boundary result should have zero volume");
}

void test_order_book_replayer_replaces_existing_order_at_same_id() {
  OrderBookReplayer replayer;

  const auto initial_packet = make_packet(
      "7203", 5000, 50,
      {make_add_message(501, 'B', 1000, tse_mbo::make_price(20))});

  const auto replacement_packet = make_packet(
      "7203", 5001, 51,
      {make_add_message(501, 'B', 700, tse_mbo::make_price(21))});

  replayer.apply(initial_packet);
  replayer.apply(replacement_packet);

  const auto& issue = replayer.issues().at("7203");
  expect(issue.live_orders.size() == 1, "Replacement should keep one live order");
  expect(issue.live_orders.at(501).quantity == 700, "Replacement should update order quantity");
  expect_price(issue.live_orders.at(501).price, tse_mbo::make_price(21),
               "Replacement should update order price");
  expect(issue.limit_price_levels.size() == 1, "Replacement should leave one price level");
  expect(issue.limit_price_levels.at(tse_mbo::make_price(21)).bid_volume == 700,
         "New price level should be populated");
  expect(issue.limit_price_levels.find(tse_mbo::make_price(20)) == issue.limit_price_levels.end(),
         "Old price level should be removed");
}

void test_indicative_match_supports_market_orders() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "7203";
  issue_state.market_bid_volume = 15;
  issue_state.market_ask_volume = 15;
  issue_state.limit_price_levels[tse_mbo::make_price(20)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(20)].ask_volume = 5;

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "Market orders should still allow a match");
  expect_price(result.price, tse_mbo::make_price(20), "The single candidate price should be selected");
  expect(result.volume == 20, "Market orders should contribute to IAV");
}

void test_indicative_match_uses_direction_reversal_tie_break() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "7203";
  issue_state.previous_reference_price = tse_mbo::make_price(10);
  issue_state.limit_price_levels[tse_mbo::make_price(10)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(20)].ask_volume = 5;

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "Reversal search should still yield a result");
  expect_price(result.price, tse_mbo::make_price(10), "Tie reversal should keep the current cursor candidate");
  expect(result.volume == 0, "The candidate volume should be preserved");
}

void test_indicative_match_picks_closest_to_reference_price() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "7203";
  issue_state.previous_reference_price = tse_mbo::make_price(13);
  issue_state.limit_price_levels[tse_mbo::make_price(10)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(20)].ask_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(15)].bid_volume = 0;
  issue_state.limit_price_levels[tse_mbo::make_price(15)].ask_volume = 0;

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "Multi-valid-price book should produce a match");
  expect_price(result.price, tse_mbo::make_price(15),
               "Should pick the in-band price closest to the previous reference price");
  expect(result.volume == 0,
         "IAV at the chosen price should be min(cum_bid, cum_ask)");
}

void test_indicative_match_accepts_band_edge_with_zero_tip() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "7203";
  issue_state.previous_reference_price = tse_mbo::make_price(10);
  issue_state.limit_price_levels[tse_mbo::make_price(10)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(10)].ask_volume = 3;

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "Single price with both sides should match");
  expect_price(result.price, tse_mbo::make_price(10),
               "Single in-band price should be selected");
  expect(result.volume == 3, "IAV should be the smaller side");
}

void test_indicative_match_returns_zero_volume_for_non_crossing_book() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "7203";
  issue_state.limit_price_levels[tse_mbo::make_price(10)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(20)].ask_volume = 5;
  issue_state.previous_reference_price = tse_mbo::make_price(20);

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result,
         "Non-crossing book yields a TSE-band result at the boundary prices");
  expect_price(result.price, tse_mbo::make_price(20),
               "Closest in-band price to the reference should win");
  expect(result.volume == 0,
         "Non-crossing book should produce zero match volume");
}

// Cond 3 takes precedence over Cond 5: when two band prices have equal IAV,
// the price with smaller |cum_bid - cum_ask| wins, even if it's farther from ref.
// Construction:
//   bid 100 at 1742 and 1777, market_ask=300, no asks anywhere.
//   At 1742: cum_bid = 200, cum_ask = 300, tip_up = 100+100=oops let me trace
// Simpler: use volumes that hand-trace to give two band prices with different imbalance.
void test_indicative_match_cond3_min_imbalance_beats_cond5() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "C3_TEST";
  // Asks: market=10, plus limit ask 10 at price 1100.
  // Bids: 5 at 900, 5 at 1000, 5 at 1100.
  // cum_ask (low->high, +market 10):
  //   900: 10, 1000: 10, 1100: 20.
  // cum_bid (high->low, +market 0):
  //   1100: 5, 1000: 10, 900: 15.
  // tip_up=cum_ask-cum_bid+bid: 900: 10-15+5=0, 1000: 10-10+5=5, 1100: 20-5+5=20.
  // tip_down=cum_bid-cum_ask+ask: 900: 15-10+0=5, 1000: 10-10+0=0, 1100: 5-20+10=-5.
  // Band = {900, 1000}. IAV at 900 = min(15,10)=10. IAV at 1000 = min(10,10)=10. Equal.
  // Imbalance at 900 = |15-10|=5. Imbalance at 1000 = |10-10|=0. Cond 3 picks 1000.
  // ref=900 makes Cond 5 want 900 (closer), but Cond 3 overrides -> 1000.
  issue_state.market_ask_volume = 10;
  issue_state.limit_price_levels[tse_mbo::make_price(900)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(1000)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(1100)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(1100)].ask_volume = 10;
  issue_state.previous_reference_price = tse_mbo::make_price(900);

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "Cond 3 test should match");
  expect_price(result.price, tse_mbo::make_price(1000),
               "Cond 3 (min imbalance) overrides Cond 5 (closest to ref)");
  expect(result.volume == 10, "Cond 3 test IAV should be 10");
}

// Cond 5.1: when reference price is above an entire Cond 3 band of mixed-side
// imbalances, pick the highest. Construct a flat book where two prices have
// identical (zero) imbalance and ref is above both.
void test_indicative_match_cond5_1_ref_above_band() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "C5_1";
  // bid=ask at price 100, bid=ask at price 110. Single-tick book at each price.
  // cum_ask (low->high): 100->5, 110->10.
  // cum_bid (high->low): 110->5, 100->10.
  // tip_up: 100: 5-10+5=0; 110: 10-5+5=10.
  // tip_down: 100: 10-5+5=10; 110: 5-10+5=0.
  // Both in band, IAV = min(cb,ca) = 5 at each. Imbalance = 5 at each.
  // Cond 3 keeps both. Signed imb at 100 = ca-cb = -5 (buy side),
  //                  at 110 = ca-cb = 5 (sell side). Mixed -> Cond 5.
  // ref=200 > both -> highest = 110.
  issue_state.limit_price_levels[tse_mbo::make_price(100)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(100)].ask_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(110)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(110)].ask_volume = 5;
  issue_state.previous_reference_price = tse_mbo::make_price(200);

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "C5.1 test should match");
  expect_price(result.price, tse_mbo::make_price(110),
               "Cond 5.1: ref above Cond 3 band -> pick highest");
}

// Cond 5.3: reference price below entire Cond 3 band -> pick lowest.
void test_indicative_match_cond5_3_ref_below_band() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "C5_3";
  // Same flat book as 5.1 but ref below.
  issue_state.limit_price_levels[tse_mbo::make_price(100)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(100)].ask_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(110)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(110)].ask_volume = 5;
  issue_state.previous_reference_price = tse_mbo::make_price(50);

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "C5.3 test should match");
  expect_price(result.price, tse_mbo::make_price(100),
               "Cond 5.3: ref below Cond 3 band -> pick lowest");
}

// Cond 5.2: reference price inside the Cond 3 band -> pick reference.
void test_indicative_match_cond5_2_ref_inside_band() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "C5_2";
  // Three flat-imbalance prices: 100, 105, 110. ref=105.
  issue_state.limit_price_levels[tse_mbo::make_price(100)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(100)].ask_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(105)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(105)].ask_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(110)].bid_volume = 5;
  issue_state.limit_price_levels[tse_mbo::make_price(110)].ask_volume = 5;
  issue_state.previous_reference_price = tse_mbo::make_price(105);

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(result.has_result, "C5.2 test should match");
  expect_price(result.price, tse_mbo::make_price(105),
               "Cond 5.2: ref inside Cond 3 band -> pick reference itself");
}

// Market orders alone (no limits) cannot form a contract price.
void test_indicative_match_market_only_no_match() {
  tse_mbo::IssueState issue_state;
  issue_state.issue_code = "MKTONLY";
  issue_state.market_ask_volume = 10;
  issue_state.market_bid_volume = 5;

  const auto result = tse_mbo::calculate_indicative_match(issue_state);

  expect(!result.has_result, "Market-only book has no in-band price");
}

void test_app_replays_multiple_pcaps_by_capture_time() {
  const auto temp_dir = std::filesystem::temp_directory_path();
  const auto early_path = temp_dir / "tse_mbo_timestamp_early.pcap.gz";
  const auto late_path = temp_dir / "tse_mbo_timestamp_late.pcap.gz";
  const auto output_path = temp_dir / "tse_mbo_timestamp_result.csv";

  const auto early_payload = make_flex_packet("7203", 1, 1, {make_add_tag(901, 'B', 100, 100000)});
  const auto late_payload = make_flex_packet("7203", 2, 2, {make_delete_tag(901)});
  write_gzip_file(early_path, make_classic_pcap_bytes(make_udp_capture_record(early_payload, 100, 0)));
  write_gzip_file(late_path, make_classic_pcap_bytes(make_udp_capture_record(late_payload, 200, 0)));

  tse_mbo::AppConfig config;
  config.pcap_paths = {late_path, early_path};
  config.csv_output_path = output_path;
  config.summary_only = true;

  const int status = tse_mbo::run(config);
  expect(status == 0, "Merged replay app run should succeed");

  std::ifstream output(output_path);
  std::string header;
  std::string row;
  std::getline(output, header);
  std::getline(output, row);

  expect(header == "symbol,iap,iav", "Merged replay CSV header mismatch");
  expect(row == "7203,0.0000,0", "Merged replay should apply early add before late delete");

  std::filesystem::remove(early_path);
  std::filesystem::remove(late_path);
  std::filesystem::remove(output_path);
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
    test_indicative_match_picks_closest_to_reference_price();
    test_indicative_match_accepts_band_edge_with_zero_tip();
    test_indicative_match_returns_zero_volume_for_non_crossing_book();
    test_indicative_match_cond3_min_imbalance_beats_cond5();
    test_indicative_match_cond5_1_ref_above_band();
    test_indicative_match_cond5_2_ref_inside_band();
    test_indicative_match_cond5_3_ref_below_band();
    test_indicative_match_market_only_no_match();
    test_app_replays_multiple_pcaps_by_capture_time();
  } catch (const std::exception& ex) {
    std::cerr << "Test failure: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "All tests passed.\n";
  return 0;
}
