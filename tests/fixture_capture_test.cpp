#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cerrno>
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
#include "flex/flex_parser.hpp"
#include "ingest/network_decoder.hpp"
#include "ingest/pcap_reader.hpp"
#include "audit/iep_iev_audit.hpp"

namespace {

using tse_mbo::FlexPacketView;
using tse_mbo::FlexParser;
using tse_mbo::NetworkDecoder;
using tse_mbo::OrderBookReplayer;
using tse_mbo::PcapReader;
using tse_mbo::UdpDatagramView;

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
  std::map<char, std::uint64_t> tag_counts;
  std::map<std::string, std::uint64_t> endpoint_counts;
  std::map<std::string, std::uint64_t> issue_packet_counts;
  std::map<std::string, std::map<std::string, std::uint64_t>> issue_packet_counts_by_file;
  std::vector<std::string> sample_lines;
  std::size_t sampled_datagrams = 0;
};

struct IndicativeRow {
  std::string issue_code;
  tse_mbo::IndicativeMatchResult result;
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

std::vector<IndicativeRow> build_indicative_rows(const OrderBookReplayer& replayer,
                                                 const VenueCatalog& venue_catalog) {
  std::vector<IndicativeRow> rows;
  rows.reserve(replayer.issues().size());

  for (const auto& [issue_code, issue_state] : replayer.issues()) {
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

FixtureStats inspect_fixture_inputs(const std::vector<std::filesystem::path>& pcap_paths,
                                    OrderBookReplayer& replayer) {
  PcapReader pcap_reader;
  NetworkDecoder network_decoder;
  FlexParser flex_parser;

  FixtureStats stats;

  for (const auto& pcap_path : pcap_paths) {
    const auto records = pcap_reader.read_all(pcap_path);
    stats.capture_records += records.size();

    for (std::size_t record_index = 0; record_index < records.size(); ++record_index) {
      const auto datagram = network_decoder.decode_udp(records[record_index]);
      if (!datagram) {
        continue;
      }

      ++stats.udp_datagrams;
      ++stats.endpoint_counts[format_endpoint(*datagram)];

      const auto packets = flex_parser.parse_all(*datagram);
      append_sample_lines(stats, pcap_path, record_index + 1U, *datagram, packets);

      const auto file_name = pcap_path.filename().string();
      for (const auto& packet : packets) {
        if (!packet.header.issue_code.empty()) {
          ++stats.issue_packet_counts[packet.header.issue_code];
          ++stats.issue_packet_counts_by_file[file_name][packet.header.issue_code];
        }
        for (const auto& tag : packet.tags) {
          ++stats.tag_counts[tag.message_type];
        }
        replayer.apply(packet);
      }
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
                                           const std::vector<std::filesystem::path>& pcap_paths) {
  const std::filesystem::path results_dir{TSE_MBO_RESULTS_DIR};
  std::filesystem::create_directories(results_dir);

  const auto report_path = results_dir / "step1_fixture_report.txt";
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
  out << "  issues_tracked=" << issues_tracked << "\n";

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
                                           const std::vector<IndicativeRow>& indicative_rows) {
  std::filesystem::create_directories(results_dir);

  const auto csv_path = results_dir / "step3_fixture_results.csv";
  std::ofstream out(csv_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to create indicative CSV");
  }

  out << "symbol,iep,iev\n";
  for (const auto& row : indicative_rows) {
    out << row.issue_code << ',';
    if (row.result.has_result) {
      out << std::fixed << std::setprecision(4) << row.result.price;
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
                                                 const OrderBookReplayer& replayer,
                                                 const VenueCatalog& venue_catalog) {
  std::filesystem::create_directories(results_dir);

  std::vector<std::string> issue_codes;
  issue_codes.reserve(indicative_rows.size());
  for (const auto& row : indicative_rows) {
    issue_codes.push_back(row.issue_code);
  }

  const auto csv_path = results_dir / "iep_iev_audit.csv";
  std::ofstream out(csv_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("Failed to create indicative audit CSV");
  }

  tse_mbo::audit::write_indicative_audit_csv(
      out, issue_codes, replayer.issues(), venue_catalog.issue_names);
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
    OrderBookReplayer replayer;
    const auto fixture_stats = inspect_fixture_inputs(pcap_paths, replayer);
    const auto& replay_stats = replayer.stats();
    const auto indicative_rows = build_indicative_rows(replayer, venue_catalog);
    const auto csv_path = write_indicative_csv(std::filesystem::path{TSE_MBO_RESULTS_DIR}, indicative_rows);
    const auto audit_csv_path = write_indicative_audit_csv(
        std::filesystem::path{TSE_MBO_RESULTS_DIR}, indicative_rows, replayer, venue_catalog);
    const auto report_path = write_fixture_report(
        fixture_stats, venue_catalog, replay_stats, indicative_rows, replayer.issues().size(), pcap_paths);

    expect(fixture_stats.capture_records == 210990, "Unexpected capture record count");
    expect(fixture_stats.udp_datagrams == 210990, "Unexpected UDP datagram count");
    expect(replay_stats.packets_parsed == 210990, "Unexpected FLEX packet count");
    expect(replay_stats.tags_seen == 430530, "Unexpected total tag count");
    expect(replay_stats.add_tags == 188936, "Unexpected add tag count");
    expect(replay_stats.delete_tags == 30261, "Unexpected delete tag count");
    expect(replay_stats.executed_tags == 0, "Unexpected executed tag count");
    expect(replay_stats.executed_with_price_tags == 0, "Unexpected executed-with-price tag count");
    expect(replay_stats.reset_tags == 0, "Unexpected reset tag count");
    expect(replayer.issues().size() == 344, "Unexpected issue count");

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

    std::cout << "Fixture report written to " << report_path << "\n";
    std::cout << "Indicative CSV written to " << csv_path << "\n";
    std::cout << "Indicative audit CSV written to " << audit_csv_path << "\n";
    std::cout << "Step 1 fixture checks passed.\n";
  } catch (const std::exception& ex) {
    std::cerr << "Test failure: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
