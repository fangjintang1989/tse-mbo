#include "app/app.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "book/order_book.hpp"
#include "flex/flex_parser.hpp"
#include "ingest/network_decoder.hpp"
#include "ingest/pcap_reader.hpp"

namespace tse_mbo {

namespace {

struct VenueInstrument {
  std::string symbol;
  std::string security_type;
};

using VenueCatalog = std::unordered_map<std::string, VenueInstrument>;

struct CsvRow {
  std::string symbol;
  IndicativeMatchResult result;
};

bool is_stock_security_type(std::string_view security_type) {
  return security_type == "01" || security_type == "02" || security_type == "03" ||
         security_type == "04";
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

VenueCatalog load_venue_catalog(const std::filesystem::path& venue_json_path) {
  std::ifstream input(venue_json_path);
  if (!input.is_open()) {
    throw std::runtime_error("Failed to open venue JSON file");
  }

  VenueCatalog catalog;
  std::string line;
  while (std::getline(input, line)) {
    if (line.find("\"TseFullInstrument\"") == std::string::npos) {
      continue;
    }

    const auto symbol = extract_quoted_value(line, "\"exchSymbol\": \"");
    const auto security_type = extract_quoted_value(line, "\"securityType\": \"");
    if (symbol.empty() || !is_stock_security_type(security_type)) {
      continue;
    }

    catalog.insert_or_assign(symbol, VenueInstrument{symbol, security_type});
  }

  return catalog;
}

void print_usage_impl() {
  std::cout
      << "Usage:\n"
      << "  tse_mbo --pcap <file> [--pcap <file> ...] [--venue-json <file>] [--csv-out <file>] "
         "[--summary-only]\n";
}

std::vector<CsvRow> build_csv_rows(const OrderBookReplayer& replayer, const VenueCatalog* venue_catalog) {
  std::vector<CsvRow> rows;

  if (venue_catalog != nullptr) {
    for (const auto& [symbol, instrument] : *venue_catalog) {
      if (instrument.symbol.empty()) {
        continue;
      }
      const auto issue_it = replayer.issues().find(symbol);
      if (issue_it == replayer.issues().end()) {
        continue;
      }
      if (symbol == "<control>") {
        continue;
      }
      rows.push_back(CsvRow{symbol, issue_it->second.last_indicative_match});
    }
  } else {
    for (const auto& [symbol, issue_state] : replayer.issues()) {
      if (symbol == "<control>") {
        continue;
      }
      rows.push_back(CsvRow{symbol, issue_state.last_indicative_match});
    }
  }

  std::sort(rows.begin(), rows.end(), [](const CsvRow& lhs, const CsvRow& rhs) {
    return lhs.symbol < rhs.symbol;
  });

  return rows;
}

void write_csv(std::ostream& out, const std::vector<CsvRow>& rows) {
  out << "symbol,iep,iev\n";
  for (const auto& row : rows) {
    out << row.symbol << ',';
    if (row.result.has_result) {
      out << std::fixed << std::setprecision(4) << row.result.price;
    } else {
      out << "0.0000";
    }
    out << ','
        << (row.result.has_result ? row.result.volume : 0) << '\n';
  }
}

void print_issue_summary(const std::unordered_map<std::string, IssueState>& issues) {
  std::size_t shown = 0;
  for (const auto& [issue_code, issue_state] : issues) {
    if (shown >= 10) {
      break;
    }
    if (issue_code == "<control>") {
      continue;
    }
    std::cout << "Issue " << issue_code << ": " << issue_state.live_orders.size()
              << " live orders, last sequence " << issue_state.last_sequence_number
              << ", last update " << issue_state.last_update_number << "\n";
    ++shown;
  }
}

}  // namespace

void print_usage() {
  print_usage_impl();
}

ParsedArgs parse_args(int argc, char* argv[]) {
  ParsedArgs result;

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};

    auto require_value = [&](const char* flag_name) -> std::optional<std::string> {
      if (index + 1 >= argc) {
        std::cerr << "Missing value after " << flag_name << ".\n";
        return std::nullopt;
      }
      ++index;
      return std::string{argv[index]};
    };

    if (arg == "--help" || arg == "-h") {
      print_usage();
      result.help_requested = true;
      return result;
    }

    if (arg == "--pcap") {
      auto value = require_value("--pcap");
      if (!value) {
        return result;
      }
      result.config.pcap_paths.emplace_back(*value);
      continue;
    }

    if (arg == "--venue-json") {
      auto value = require_value("--venue-json");
      if (!value) {
        return result;
      }
      result.config.venue_json_path = std::filesystem::path{*value};
      continue;
    }

    if (arg == "--csv-out") {
      auto value = require_value("--csv-out");
      if (!value) {
        return result;
      }
      result.config.csv_output_path = std::filesystem::path{*value};
      continue;
    }

    if (arg == "--summary-only") {
      result.config.summary_only = true;
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    return result;
  }

  if (result.config.pcap_paths.empty()) {
    std::cerr << "At least one --pcap file is required.\n";
    return result;
  }

  result.valid = true;
  return result;
}

int run(const AppConfig& config) {
  try {
    for (const auto& pcap_path : config.pcap_paths) {
      if (!std::filesystem::exists(pcap_path)) {
        std::cerr << "PCAP file not found: " << pcap_path << "\n";
        return 1;
      }
    }

    if (config.venue_json_path && !std::filesystem::exists(*config.venue_json_path)) {
      std::cerr << "Venue JSON file not found: " << *config.venue_json_path << "\n";
      return 1;
    }

    if (config.csv_output_path) {
      const auto parent = config.csv_output_path->parent_path();
      if (!parent.empty()) {
        std::filesystem::create_directories(parent);
      }
    }

    PcapReader pcap_reader;
    NetworkDecoder network_decoder;
    FlexParser flex_parser;
    OrderBookReplayer replayer;

    VenueCatalog venue_catalog;
    const VenueCatalog* venue_catalog_ptr = nullptr;
    if (config.venue_json_path) {
      venue_catalog = load_venue_catalog(*config.venue_json_path);
      venue_catalog_ptr = &venue_catalog;
    }

    std::uint64_t total_capture_records = 0;
    std::uint64_t total_udp_datagrams = 0;

    for (const auto& pcap_path : config.pcap_paths) {
      const auto records = pcap_reader.read_all(pcap_path);
      total_capture_records += records.size();

      for (std::size_t record_index = 0; record_index < records.size(); ++record_index) {
        const auto& record = records[record_index];
        const auto datagram = network_decoder.decode_udp(record);
        if (!datagram) {
          continue;
        }
        ++total_udp_datagrams;

        const auto packets = flex_parser.parse_all(*datagram);
        if (packets.empty()) {
          continue;
        }
        for (const auto& packet : packets) {
          replayer.apply(packet);
        }
      }
    }

    const auto& stats = replayer.stats();
    std::cout << "Capture records: " << total_capture_records << "\n";
    std::cout << "UDP datagrams: " << total_udp_datagrams << "\n";
    std::cout << "FLEX packets parsed: " << stats.packets_parsed << "\n";
    std::cout << "Tags seen: " << stats.tags_seen << "\n";
    std::cout << "A tags: " << stats.add_tags << "\n";
    std::cout << "D tags: " << stats.delete_tags << "\n";
    std::cout << "E tags: " << stats.executed_tags << "\n";
    std::cout << "C tags: " << stats.executed_with_price_tags << "\n";
    std::cout << "R tags: " << stats.reset_tags << "\n";
    std::cout << "Issues tracked: " << replayer.issues().size() << "\n";

    if (!config.summary_only) {
      print_issue_summary(replayer.issues());
    }

    if (config.csv_output_path) {
      const auto rows = build_csv_rows(replayer, venue_catalog_ptr);
      std::ofstream output(*config.csv_output_path, std::ios::out | std::ios::trunc);
      if (!output.is_open()) {
        std::cerr << "Failed to open CSV output file: " << *config.csv_output_path << "\n";
        return 1;
      }
      write_csv(output, rows);
      std::cout << "CSV written to " << *config.csv_output_path << "\n";
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}

}  // namespace tse_mbo
