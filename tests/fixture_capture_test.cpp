#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iconv.h>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "book/order_book.hpp"
#include "book/indicative.hpp"
#include "flex/flex_message.hpp"
#include "flex/flex_parser.hpp"
#include "ingest/network_decoder.hpp"
#include "ingest/pcap_reader.hpp"
#include "audit/iap_iav_audit.hpp"
#include "tse/tse.hpp"

namespace {

using tse_mbo::FlexPacketView;
using tse_mbo::FlexParser;
using tse_mbo::NetworkDecoder;
using tse_mbo::PcapReader;
using tse_mbo::UdpDatagramView;
using tse_mbo::CaptureRecord;
using tse_mbo::Tse;

void expect(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string format_ipv4(std::uint32_t ipv4) {
  std::ostringstream out;
  out << ((ipv4 >> 24U) & 0xffU) << '.'
      << ((ipv4 >> 16U) & 0xffU) << '.'
      << ((ipv4 >> 8U) & 0xffU) << '.'
      << (ipv4 & 0xffU);
  return out.str();
}

std::string format_endpoint(const UdpDatagramView& datagram) {
  std::ostringstream out;
  out << format_ipv4(datagram.src_ipv4) << ':' << datagram.src_port
      << " -> " << format_ipv4(datagram.dst_ipv4) << ':' << datagram.dst_port;
  return out.str();
}

void write_csv_escaped(std::ostream& out, std::string_view value) {
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

std::string format_tag_hex(const std::vector<std::byte>& bytes) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index != 0) {
      out << ' ';
    }
    out << std::setw(2)
        << static_cast<unsigned int>(std::to_integer<unsigned char>(bytes[index]));
  }
  return out.str();
}

std::uint32_t read_be_u32(const std::vector<std::byte>& bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 8U) |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3]));
}

std::uint64_t read_be_u48(const std::vector<std::byte>& bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 40U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 32U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 24U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 16U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 4])) << 8U) |
         static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 5]));
}

std::uint64_t read_be_u64(const std::vector<std::byte>& bytes, std::size_t offset) {
  return (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 56U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 48U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 40U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 32U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 4])) << 24U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 5])) << 16U) |
         (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 6])) << 8U) |
         static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + 7]));
}

std::string format_price(std::uint64_t raw_price) {
  if (raw_price == tse_mbo::kRawMarketOrderPrice) {
    return "MARKET";
  }

  std::ostringstream out;
  out << std::fixed << std::setprecision(4)
      << tse_mbo::price_to_double(static_cast<tse_mbo::Price>(raw_price));
  return out.str();
}

struct CaptureTimestamp {
  std::uint32_t sec = 0;
  std::uint32_t nsec = 0;
};

int compare_timestamp(const CaptureTimestamp& lhs, const CaptureTimestamp& rhs) {
  if (lhs.sec < rhs.sec) {
    return -1;
  }
  if (lhs.sec > rhs.sec) {
    return 1;
  }
  if (lhs.nsec < rhs.nsec) {
    return -1;
  }
  if (lhs.nsec > rhs.nsec) {
    return 1;
  }
  return 0;
}

std::string format_epoch_nsec(const CaptureTimestamp& timestamp) {
  std::ostringstream out;
  out << timestamp.sec << '.' << std::setw(9) << std::setfill('0') << timestamp.nsec;
  return out.str();
}

std::string format_utc_or_jst(const CaptureTimestamp& timestamp, int hour_offset) {
  const std::time_t adjusted_sec =
      static_cast<std::time_t>(timestamp.sec) + static_cast<std::time_t>(hour_offset) * 3600;
  std::tm time_parts{};
  if (const auto* utc_time = std::gmtime(&adjusted_sec)) {
    time_parts = *utc_time;
  }

  std::ostringstream out;
  out << std::put_time(&time_parts, "%Y-%m-%dT%H:%M:%S")
      << '.' << std::setw(9) << std::setfill('0') << timestamp.nsec
      << (hour_offset == 0 ? "Z" : "+09:00");
  return out.str();
}

std::uint64_t elapsed_nsec(const CaptureTimestamp& start, const CaptureTimestamp& end) {
  const auto sec_delta = static_cast<std::uint64_t>(end.sec - start.sec);
  if (end.nsec >= start.nsec) {
    return sec_delta * 1000000000ULL + static_cast<std::uint64_t>(end.nsec - start.nsec);
  }
  return (sec_delta - 1U) * 1000000000ULL +
         (1000000000ULL + static_cast<std::uint64_t>(end.nsec) - start.nsec);
}

std::string format_elapsed_nsec(std::uint64_t elapsed) {
  std::ostringstream out;
  out << (elapsed / 1000000000ULL) << '.'
      << std::setw(9) << std::setfill('0') << (elapsed % 1000000000ULL)
      << "s";
  return out.str();
}

struct CaptureTimeRange {
  bool has_value = false;
  CaptureTimestamp first;
  CaptureTimestamp last;
  CaptureTimestamp min;
  CaptureTimestamp max;
};

void update_capture_time_range(CaptureTimeRange& range, CaptureTimestamp timestamp) {
  if (!range.has_value) {
    range.has_value = true;
    range.first = timestamp;
    range.last = timestamp;
    range.min = timestamp;
    range.max = timestamp;
    return;
  }

  range.last = timestamp;
  if (compare_timestamp(timestamp, range.min) < 0) {
    range.min = timestamp;
  }
  if (compare_timestamp(timestamp, range.max) > 0) {
    range.max = timestamp;
  }
}

bool is_issue_name_padding(std::uint32_t code_unit) {
  return code_unit == 0U || code_unit == 32U || code_unit == 8224U || code_unit == 12288U;
}

std::string extract_quoted_value(std::string_view text, std::string_view key) {
  const auto start = text.find(key);
  if (start == std::string_view::npos) {
    return {};
  }

  const auto value_start = start + key.size();
  const auto value_end = text.find('"', value_start);
  if (value_end == std::string_view::npos) {
    return {};
  }

  return std::string{text.substr(value_start, value_end - value_start)};
}

std::vector<std::uint32_t> parse_issue_name_code_units(std::string_view text) {
  constexpr std::string_view key = "\"fullIssueName\": [";
  const auto start = text.find(key);
  if (start == std::string_view::npos) {
    return {};
  }

  const auto values_start = start + key.size();
  const auto values_end = text.find(']', values_start);
  if (values_end == std::string_view::npos) {
    return {};
  }

  std::vector<std::uint32_t> code_units;
  std::size_t pos = values_start;
  while (pos < values_end) {
    while (pos < values_end && (text[pos] == ' ' || text[pos] == ',')) {
      ++pos;
    }
    if (pos >= values_end) {
      break;
    }

    std::uint32_t value = 0;
    const auto* begin = text.data() + static_cast<std::ptrdiff_t>(pos);
    const auto* end = text.data() + static_cast<std::ptrdiff_t>(values_end);
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{}) {
      break;
    }

    code_units.push_back(value);
    pos = static_cast<std::size_t>(result.ptr - text.data());
  }

  while (!code_units.empty() && is_issue_name_padding(code_units.back())) {
    code_units.pop_back();
  }

  return code_units;
}

std::string cp932_to_utf8(const std::string& cp932_text) {
  iconv_t converter = iconv_open("UTF-8//IGNORE", "CP932");
  if (converter == reinterpret_cast<iconv_t>(-1)) {
    return {};
  }

  std::string output(cp932_text.size() * 4U + 4U, '\0');
  char* input_ptr = const_cast<char*>(cp932_text.data());
  std::size_t input_bytes = cp932_text.size();
  char* output_ptr = output.data();
  std::size_t output_bytes = output.size();

  while (input_bytes > 0) {
    if (iconv(converter, &input_ptr, &input_bytes, &output_ptr, &output_bytes) !=
        static_cast<std::size_t>(-1)) {
      break;
    }
    if (errno != E2BIG) {
      iconv_close(converter);
      return {};
    }

    const auto used = static_cast<std::size_t>(output_ptr - output.data());
    output.resize(output.size() * 2U);
    output_ptr = output.data() + used;
    output_bytes = output.size() - used;
  }

  output.resize(static_cast<std::size_t>(output_ptr - output.data()));
  iconv_close(converter);
  return output;
}

std::string decode_issue_name(const std::vector<std::uint32_t>& code_units) {
  std::string cp932_bytes;
  for (const auto code_unit : code_units) {
    if (is_issue_name_padding(code_unit)) {
      continue;
    }
    if (code_unit <= 0xffU) {
      cp932_bytes.push_back(static_cast<char>(code_unit));
      continue;
    }
    cp932_bytes.push_back(static_cast<char>((code_unit >> 8U) & 0xffU));
    cp932_bytes.push_back(static_cast<char>(code_unit & 0xffU));
  }
  return cp932_to_utf8(cp932_bytes);
}

struct VenueCatalog {
  std::map<std::string, std::string> issue_names;
  std::unordered_set<std::string> stock_issue_codes;
  std::unordered_map<std::string, tse_mbo::Price> base_prices;
};

VenueCatalog load_venue_catalog(const std::filesystem::path& venue_json_path) {
  std::ifstream input(venue_json_path);
  if (!input.is_open()) {
    throw std::runtime_error("Failed to open venue JSON fixture");
  }

  VenueCatalog catalog;
  std::string line;
  while (std::getline(input, line)) {
    if (line.find("\"TseFullInstrument\"") == std::string::npos) {
      continue;
    }

    const auto issue_code = extract_quoted_value(line, "\"exchSymbol\": \"");
    const auto security_type = extract_quoted_value(line, "\"securityType\": \"");
    if (issue_code.empty()) {
      continue;
    }

    const auto full_issue_name = decode_issue_name(parse_issue_name_code_units(line));
    if (!full_issue_name.empty()) {
      catalog.issue_names.insert_or_assign(issue_code, full_issue_name);
    }
    if (security_type == "01" || security_type == "02" || security_type == "03" || security_type == "04") {
      catalog.stock_issue_codes.insert(issue_code);
    }
    {
      const auto base_price_key = std::string{"\"basePrice\":"};
      const auto pos = line.find(base_price_key);
      if (pos != std::string::npos) {
        auto cursor = pos + base_price_key.size();
        while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
          ++cursor;
        }
        const auto value_start = cursor;
        while (cursor < line.size() &&
               (std::isdigit(static_cast<unsigned char>(line[cursor])) || line[cursor] == '-')) {
          ++cursor;
        }
        if (cursor > value_start) {
          try {
            const auto raw = std::stoll(line.substr(value_start, cursor - value_start));
            catalog.base_prices.insert_or_assign(
                issue_code, raw * tse_mbo::kPriceScale);
          } catch (...) {
          }
        }
      }
    }
  }

  return catalog;
}

std::string lookup_issue_name(const VenueCatalog& catalog, const std::string& issue_code) {
  const auto it = catalog.issue_names.find(issue_code);
  if (it == catalog.issue_names.end()) {
    return "<missing venue name>";
  }
  return it->second;
}

struct FixtureStats {
  std::uint64_t capture_records = 0;
  std::uint64_t udp_datagrams = 0;
  std::uint64_t decoded_message_rows = 0;
  std::map<char, std::uint64_t> tag_counts;
  std::map<std::string, std::uint64_t> endpoint_counts;
  std::map<std::string, std::uint64_t> issue_packet_counts;
  std::map<std::string, std::map<std::string, std::uint64_t>> issue_packet_counts_by_file;
  std::map<std::string, CaptureTimeRange> capture_time_ranges_by_file;
  std::vector<std::string> sample_lines;
  std::size_t sampled_datagrams = 0;
};

struct IndicativeRow {
  std::string issue_code;
  tse_mbo::IndicativeMatchResult result;
};

struct FixtureReplayRecord {
  CaptureRecord record;
  std::filesystem::path pcap_path;
  std::size_t file_index = 0;
  std::size_t record_index = 0;
};

void append_sample_lines(FixtureStats& stats,
                         const std::filesystem::path& pcap_path,
                         std::size_t record_index,
                         const UdpDatagramView& datagram,
                         const std::vector<FlexPacketView>& packets) {
  constexpr std::size_t kMaxSampledDatagrams = 8;
  if (stats.sampled_datagrams >= kMaxSampledDatagrams) {
    return;
  }

  std::ostringstream udp_line;
  udp_line << "udp"
           << " file=" << pcap_path.filename().string()
           << " record=" << record_index
           << " ts_sec=" << datagram.capture_ts_sec
           << " ts_subsec=" << datagram.capture_ts_subsec
           << " endpoint=\"" << format_endpoint(datagram) << '"'
           << " payload_bytes=" << datagram.payload.size()
           << " flex_packets=" << packets.size();
  stats.sample_lines.push_back(udp_line.str());

  for (std::size_t packet_index = 0; packet_index < packets.size(); ++packet_index) {
    const auto& packet = packets[packet_index];

    std::ostringstream packet_line;
    packet_line << "  flex"
                << " index=" << (packet_index + 1U)
                << " seq=" << packet.header.sequence_number
                << " issue=" << (packet.header.issue_code.empty() ? "<control>" : packet.header.issue_code)
                << " update=" << packet.header.update_number
                << " packet=" << static_cast<unsigned int>(packet.header.packet_number) << '/'
                << static_cast<unsigned int>(packet.header.total_number_of_packets)
                << " utility=" << static_cast<unsigned int>(packet.header.utility_flag)
                << " messages=" << static_cast<unsigned int>(packet.header.message_count);
    stats.sample_lines.push_back(packet_line.str());

    for (std::size_t tag_index = 0; tag_index < packet.tags.size(); ++tag_index) {
      const auto& tag = packet.tags[tag_index];

      std::ostringstream tag_line;
      tag_line << "    tag"
               << " index=" << (tag_index + 1U)
               << " type=" << tag.message_type
               << " length=" << static_cast<unsigned int>(tag.length)
               << " hex=" << format_tag_hex(tag.bytes);
      stats.sample_lines.push_back(tag_line.str());
    }
  }

  ++stats.sampled_datagrams;
}

std::vector<IndicativeRow> build_indicative_rows(const Tse& tse,
                                                 const VenueCatalog& venue_catalog) {
  std::vector<IndicativeRow> rows;
  rows.reserve(tse.issues().size());

  for (const auto& [issue_code, issue_state] : tse.issues()) {
    if (issue_code == "<control>") {
      continue;
    }
    if (!venue_catalog.stock_issue_codes.contains(issue_code)) {
      continue;
    }
    rows.push_back(IndicativeRow{issue_code, tse_mbo::calculate_indicative_match(issue_state)});
  }

  std::sort(rows.begin(), rows.end(), [](const IndicativeRow& lhs, const IndicativeRow& rhs) {
    return lhs.issue_code < rhs.issue_code;
  });

  return rows;
}

void write_step1_decoded_messages_header(std::ostream& out) {
  out << "file,record_index,capture_epoch_nsec,capture_utc,capture_jst,endpoint,"
         "payload_bytes,flex_packet_index,sequence_number,issue_code,update_number,"
         "packet_number,total_packets,utility_flag,message_count,tag_index,tag_type,"
         "tag_length,order_id,side,quantity,raw_price,price,order_condition,"
         "modification_flag,executed_quantity,tag_hex\n";
}

void write_step1_decoded_message_row(std::ostream& out,
                                     const std::filesystem::path& pcap_path,
                                     std::size_t record_index,
                                     const UdpDatagramView& datagram,
                                     std::size_t packet_index,
                                     const FlexPacketView& packet,
                                     std::size_t tag_index,
                                     const tse_mbo::FlexTagView& tag) {
  const CaptureTimestamp timestamp{datagram.capture_ts_sec, datagram.capture_ts_subsec};

  std::string order_id;
  std::string side;
  std::string quantity;
  std::string raw_price;
  std::string price;
  std::string order_condition;
  std::string modification_flag;
  std::string executed_quantity;

  if (tag.message_type == 'A' && tag.bytes.size() >= 26) {
    const auto decoded_raw_price = read_be_u64(tag.bytes, 16);
    order_id = std::to_string(read_be_u32(tag.bytes, 5));
    side.push_back(static_cast<char>(std::to_integer<std::uint8_t>(tag.bytes[9])));
    quantity = std::to_string(read_be_u48(tag.bytes, 10));
    raw_price = std::to_string(decoded_raw_price);
    price = format_price(decoded_raw_price);
    order_condition = std::to_string(std::to_integer<std::uint8_t>(tag.bytes[24]));
    modification_flag = std::to_string(std::to_integer<std::uint8_t>(tag.bytes[25]));
  } else if (tag.message_type == 'D' && tag.bytes.size() >= 11) {
    order_id = std::to_string(read_be_u32(tag.bytes, 5));
  } else if ((tag.message_type == 'E' || tag.message_type == 'C') && tag.bytes.size() >= 20) {
    order_id = std::to_string(read_be_u32(tag.bytes, 5));
    executed_quantity = std::to_string(read_be_u48(tag.bytes, 10));
  }

  out << pcap_path.filename().string() << ','
      << record_index << ','
      << format_epoch_nsec(timestamp) << ','
      << format_utc_or_jst(timestamp, 0) << ','
      << format_utc_or_jst(timestamp, 9) << ',';
  write_csv_escaped(out, format_endpoint(datagram));
  out << ',' << datagram.payload.size() << ','
      << (packet_index + 1U) << ','
      << packet.header.sequence_number << ','
      << (packet.header.issue_code.empty() ? "<control>" : packet.header.issue_code) << ','
      << packet.header.update_number << ','
      << static_cast<unsigned int>(packet.header.packet_number) << ','
      << static_cast<unsigned int>(packet.header.total_number_of_packets) << ','
      << static_cast<unsigned int>(packet.header.utility_flag) << ','
      << static_cast<unsigned int>(packet.header.message_count) << ','
      << (tag_index + 1U) << ','
      << tag.message_type << ','
      << static_cast<unsigned int>(tag.length) << ','
      << order_id << ','
      << side << ','
      << quantity << ','
      << raw_price << ','
      << price << ','
      << order_condition << ','
      << modification_flag << ','
      << executed_quantity << ',';
  write_csv_escaped(out, format_tag_hex(tag.bytes));
  out << '\n';
}

FixtureStats inspect_fixture_inputs_by_timestamp(const std::vector<std::filesystem::path>& pcap_paths,
                                                 tse_mbo::ReplayDataCallback& callback,
                                                 std::ostream* step1_decoded_messages_csv) {
  PcapReader pcap_reader;
  NetworkDecoder network_decoder;
  FlexParser flex_parser;

  FixtureStats stats;
  std::vector<FixtureReplayRecord> merged_records;

  for (std::size_t file_index = 0; file_index < pcap_paths.size(); ++file_index) {
    auto records = pcap_reader.read_all(pcap_paths[file_index]);
    stats.capture_records += records.size();
    merged_records.reserve(merged_records.size() + records.size());
    for (std::size_t record_index = 0; record_index < records.size(); ++record_index) {
      merged_records.push_back(FixtureReplayRecord{
          .record = std::move(records[record_index]),
          .pcap_path = pcap_paths[file_index],
          .file_index = file_index,
          .record_index = record_index,
      });
    }
  }

  std::stable_sort(merged_records.begin(), merged_records.end(),
                   [](const FixtureReplayRecord& lhs, const FixtureReplayRecord& rhs) {
                     if (lhs.record.ts_sec != rhs.record.ts_sec) {
                       return lhs.record.ts_sec < rhs.record.ts_sec;
                     }
                     if (lhs.record.ts_subsec != rhs.record.ts_subsec) {
                       return lhs.record.ts_subsec < rhs.record.ts_subsec;
                     }
                     if (lhs.file_index != rhs.file_index) {
                       return lhs.file_index < rhs.file_index;
                     }
                     return lhs.record_index < rhs.record_index;
                   });

  for (const auto& replay_record : merged_records) {
    const auto datagram = network_decoder.decode_udp(replay_record.record);
    if (!datagram) {
      continue;
    }

    const auto file_name = replay_record.pcap_path.filename().string();
    update_capture_time_range(
        stats.capture_time_ranges_by_file[file_name],
        CaptureTimestamp{datagram->capture_ts_sec, datagram->capture_ts_subsec});
    ++stats.udp_datagrams;
    ++stats.endpoint_counts[format_endpoint(*datagram)];

    const auto packets = flex_parser.parse_all(*datagram);
    append_sample_lines(stats, replay_record.pcap_path, replay_record.record_index + 1U, *datagram, packets);

    for (std::size_t packet_index = 0; packet_index < packets.size(); ++packet_index) {
      const auto& packet = packets[packet_index];
      if (!packet.header.issue_code.empty()) {
        ++stats.issue_packet_counts[packet.header.issue_code];
        ++stats.issue_packet_counts_by_file[file_name][packet.header.issue_code];
      }
      for (std::size_t tag_index = 0; tag_index < packet.tags.size(); ++tag_index) {
        const auto& tag = packet.tags[tag_index];
        ++stats.tag_counts[tag.message_type];
        ++stats.decoded_message_rows;
        if (step1_decoded_messages_csv != nullptr) {
          write_step1_decoded_message_row(
              *step1_decoded_messages_csv, replay_record.pcap_path, replay_record.record_index + 1U,
              *datagram, packet_index, packet, tag_index, tag);
        }
      }
      callback.on_flex_packet(tse_mbo::normalize_flex_packet(packet));
    }
  }

  return stats;
}

std::vector<std::pair<std::string, std::uint64_t>> top_issue_counts(
    const std::map<std::string, std::uint64_t>& issue_packet_counts,
    std::size_t limit) {
  std::vector<std::pair<std::string, std::uint64_t>> entries{
      issue_packet_counts.begin(), issue_packet_counts.end()};

  std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.second != rhs.second) {
      return lhs.second > rhs.second;
    }
    return lhs.first < rhs.first;
  });

  if (entries.size() > limit) {
    entries.resize(limit);
  }

  return entries;
}

std::filesystem::path write_fixture_report(const FixtureStats& fixture_stats,
                                           const VenueCatalog& venue_catalog,
                                           const tse_mbo::ReplayStats& replay_stats,
                                           const std::vector<IndicativeRow>& indicative_rows,
                                           std::size_t issues_tracked,
                                           const std::vector<std::filesystem::path>& pcap_paths,
                                           bool overwrite_existing = true) {
  const std::filesystem::path results_dir{TSE_MBO_RESULTS_DIR};
  std::filesystem::create_directories(results_dir);

  const auto report_path = results_dir / "step1_fixture_report.txt";
  if (!overwrite_existing && std::filesystem::exists(report_path)) {
    return report_path;
  }

  std::ofstream out(report_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to create fixture report");
  }

  out << "# Step 1 Fixture Report\n";
  out << "\n";
  out << "inputs:\n";
  for (const auto& path : pcap_paths) {
    out << "- " << path << "\n";
  }

  out << "\n";
  out << "summary:\n";
  out << "  capture_records=" << fixture_stats.capture_records << "\n";
  out << "  udp_datagrams=" << fixture_stats.udp_datagrams << "\n";
  out << "  flex_packets=" << replay_stats.packets_parsed << "\n";
  out << "  tags_seen=" << replay_stats.tags_seen << "\n";
  out << "  decoded_message_rows=" << fixture_stats.decoded_message_rows << "\n";
  out << "  issues_tracked=" << issues_tracked << "\n";

  out << "\n";
  out << "capture_time_ranges:\n";
  out << "  precision=nanoseconds\n";
  out << "  timezone_note=pcap timestamps are stored as epoch UTC; JST is UTC+09:00\n";
  for (const auto& [file_name, range] : fixture_stats.capture_time_ranges_by_file) {
    if (!range.has_value) {
      continue;
    }
    out << "  " << file_name << ":\n";
    out << "    first_epoch_nsec=" << format_epoch_nsec(range.first) << "\n";
    out << "    first_utc=" << format_utc_or_jst(range.first, 0) << "\n";
    out << "    first_jst=" << format_utc_or_jst(range.first, 9) << "\n";
    out << "    last_epoch_nsec=" << format_epoch_nsec(range.last) << "\n";
    out << "    last_utc=" << format_utc_or_jst(range.last, 0) << "\n";
    out << "    last_jst=" << format_utc_or_jst(range.last, 9) << "\n";
    out << "    min_epoch_nsec=" << format_epoch_nsec(range.min) << "\n";
    out << "    max_epoch_nsec=" << format_epoch_nsec(range.max) << "\n";
    out << "    elapsed=" << format_elapsed_nsec(elapsed_nsec(range.min, range.max)) << "\n";
  }

  out << "\n";
  out << "tag_counts:\n";
  for (const auto& [tag_type, count] : fixture_stats.tag_counts) {
    out << "  " << tag_type << '=' << count << "\n";
  }

  out << "\n";
  out << "replay_counts:\n";
  out << "  A=" << replay_stats.add_tags << "\n";
  out << "  D=" << replay_stats.delete_tags << "\n";
  out << "  E=" << replay_stats.executed_tags << "\n";
  out << "  C=" << replay_stats.executed_with_price_tags << "\n";
  out << "  R=" << replay_stats.reset_tags << "\n";

  std::uint64_t matched_rows = 0;
  for (const auto& row : indicative_rows) {
    if (row.result.has_result) {
      ++matched_rows;
    }
  }

  out << "\n";
  out << "indicative_rows:\n";
  out << "  rows=" << indicative_rows.size() << "\n";
  out << "  rows_with_result=" << matched_rows << "\n";
  out << "  rows_without_result=" << (indicative_rows.size() - matched_rows) << "\n";

  out << "\n";
  out << "endpoints:\n";
  for (const auto& [endpoint, count] : fixture_stats.endpoint_counts) {
    out << "  " << endpoint << " count=" << count << "\n";
  }

  out << "\n";
  out << "top_issues_by_packet_count:\n";
  for (const auto& [issue_code, count] : top_issue_counts(fixture_stats.issue_packet_counts, 10)) {
    out << "  " << issue_code << " count=" << count << "\n";
  }

  out << "\n";
  out << "issue_names_by_capture:\n";
  for (const auto& [file_name, issue_counts] : fixture_stats.issue_packet_counts_by_file) {
    out << "  " << file_name << ":\n";
    for (const auto& [issue_code, count] : issue_counts) {
      out << "    " << issue_code << " -> " << lookup_issue_name(venue_catalog, issue_code)
          << " packets=" << count << "\n";
    }
  }

  out << "\n";
  out << "sample_decoded_datagrams:\n";
  for (const auto& line : fixture_stats.sample_lines) {
    out << line << "\n";
  }

  return report_path;
}

std::filesystem::path write_indicative_csv(const std::filesystem::path& results_dir,
                                           const std::vector<IndicativeRow>& indicative_rows,
                                           std::string_view file_name = "step3_iap_iav_fixture_results.csv",
                                           bool overwrite_existing = true) {
  std::filesystem::create_directories(results_dir);

  const auto csv_path = results_dir / std::string(file_name);
  if (!overwrite_existing && std::filesystem::exists(csv_path)) {
    return csv_path;
  }

  std::ofstream out(csv_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to create indicative CSV");
  }

  out << "symbol,iap,iav\n";
  for (const auto& row : indicative_rows) {
    out << row.issue_code << ',';
    if (row.result.has_result) {
      out << std::fixed << std::setprecision(4) << tse_mbo::price_to_double(row.result.price);
    } else {
      out << "0.0000";
    }
    out << ','
        << (row.result.has_result ? row.result.volume : 0) << '\n';
  }

  return csv_path;
}

std::filesystem::path write_indicative_audit_csv(const std::filesystem::path& results_dir,
                                                 const std::vector<IndicativeRow>& indicative_rows,
                                                 const Tse& tse,
                                                 const VenueCatalog& venue_catalog,
                                                 std::string_view file_name = "step2_order_book_audit.csv",
                                                 bool overwrite_existing = true) {
  std::filesystem::create_directories(results_dir);

  std::vector<std::string> issue_codes;
  issue_codes.reserve(indicative_rows.size());
  for (const auto& row : indicative_rows) {
    issue_codes.push_back(row.issue_code);
  }

  const auto csv_path = results_dir / std::string(file_name);
  if (!overwrite_existing && std::filesystem::exists(csv_path)) {
    return csv_path;
  }

  std::ofstream out(csv_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to create indicative audit CSV");
  }

  tse_mbo::audit::write_indicative_audit_csv(
      out, issue_codes, tse.issues(), venue_catalog.issue_names);
  return csv_path;
}

}  // namespace

int main() {
  try {
    const std::vector<std::filesystem::path> pcap_paths{
        std::filesystem::path{TSE_MBO_FIXTURE_PCAP_051},
        std::filesystem::path{TSE_MBO_FIXTURE_PCAP_052},
    };
    const std::filesystem::path venue_json_path{TSE_MBO_FIXTURE_VENUE_JSON};

    for (const auto& path : pcap_paths) {
      expect(std::filesystem::exists(path), "Fixture PCAP file should exist");
    }
    expect(std::filesystem::exists(venue_json_path), "Fixture venue JSON file should exist");

    const auto venue_catalog = load_venue_catalog(venue_json_path);
    Tse tse;
    for (const auto& [symbol, base_price] : venue_catalog.base_prices) {
      tse.set_base_price(symbol, base_price);
    }
    const std::filesystem::path results_dir{TSE_MBO_RESULTS_DIR};
    std::filesystem::create_directories(results_dir);
    const auto step1_decoded_csv_path = results_dir / "step1_decoded_messages.csv";
    std::ofstream step1_decoded_csv(step1_decoded_csv_path, std::ios::out | std::ios::trunc);
    if (!step1_decoded_csv.is_open()) {
      throw std::runtime_error("Failed to create step 1 decoded messages CSV");
    }
    write_step1_decoded_messages_header(step1_decoded_csv);

    const auto fixture_stats =
        inspect_fixture_inputs_by_timestamp(pcap_paths, tse, &step1_decoded_csv);
    step1_decoded_csv.close();
    const auto& replay_stats = tse.stats();
    const auto indicative_rows = build_indicative_rows(tse, venue_catalog);
    const auto csv_path = write_indicative_csv(results_dir, indicative_rows);
    const auto audit_csv_path =
        write_indicative_audit_csv(results_dir, indicative_rows, tse, venue_catalog);
    const auto report_path = write_fixture_report(
        fixture_stats, venue_catalog, replay_stats, indicative_rows, tse.issues().size(), pcap_paths);

    expect(fixture_stats.capture_records == 210990, "Unexpected capture record count");
    expect(fixture_stats.udp_datagrams == 210990, "Unexpected UDP datagram count");
    expect(replay_stats.packets_parsed == 210990, "Unexpected FLEX packet count");
    expect(replay_stats.tags_seen == 430530, "Unexpected total tag count");
    expect(fixture_stats.decoded_message_rows == replay_stats.tags_seen,
           "Step 1 decoded CSV should contain one row per parsed FLEX tag");
    expect(replay_stats.add_tags == 188936, "Unexpected add tag count");
    expect(replay_stats.delete_tags == 30261, "Unexpected delete tag count");
    expect(replay_stats.executed_tags == 0, "Unexpected executed tag count");
    expect(replay_stats.executed_with_price_tags == 0, "Unexpected executed-with-price tag count");
    expect(replay_stats.reset_tags == 0, "Unexpected reset tag count");
    expect(tse.issues().size() == 344, "Unexpected issue count");

    expect(fixture_stats.tag_counts.at('T') == 210768, "Unexpected T tag count");
    expect(fixture_stats.tag_counts.at('A') == 188936, "Unexpected A tag count");
    expect(fixture_stats.tag_counts.at('D') == 30261, "Unexpected D tag count");
    expect(fixture_stats.tag_counts.at('O') == 343, "Unexpected O tag count");
    expect(fixture_stats.tag_counts.at('L') == 222, "Unexpected L tag count");

    expect(fixture_stats.endpoint_counts.at("10.17.13.58:51551 -> 224.0.220.51:51551") == 94901,
           "Unexpected endpoint count for feed 51");
    expect(fixture_stats.endpoint_counts.at("10.17.13.68:51552 -> 224.0.220.52:51552") == 116089,
           "Unexpected endpoint count for feed 52");
    expect(fixture_stats.issue_packet_counts_by_file.at("20241105_051.test.pcap.gz").size() == 171,
           "Unexpected issue count for feed 51");
    expect(fixture_stats.issue_packet_counts_by_file.at("20241105_052.test.pcap.gz").size() == 172,
           "Unexpected issue count for feed 52");
    expect(indicative_rows.size() == 304, "Unexpected indicative output row count");
    expect(lookup_issue_name(venue_catalog, "1570") != "<missing venue name>",
           "Expected venue mapping for issue 1570");

    std::cout << "Step 1 decoded messages CSV written to " << step1_decoded_csv_path << "\n";
    std::cout << "Step 1 fixture report written to " << report_path << "\n";
    std::cout << "Step 2 order-book audit CSV written to " << audit_csv_path << "\n";
    std::cout << "Step 3 IAP/IAV CSV written to " << csv_path << "\n";
    std::cout << "Step 1/2/3 fixture checks passed.\n";
  } catch (const std::exception& ex) {
    std::cerr << "Test failure: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
