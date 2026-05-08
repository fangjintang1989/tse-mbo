#include "tse_mbo/app.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "tse_mbo/flex_parser.hpp"
#include "tse_mbo/network_decoder.hpp"
#include "tse_mbo/order_book.hpp"
#include "tse_mbo/pcap_reader.hpp"

namespace tse_mbo {

void print_usage() {
  std::cout
      << "Usage:\n"
      << "  tse_mbo --pcap <file> [--pcap <file> ...] [--venue-json <file>] [--summary-only]\n";
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

namespace {

bool exists(const std::filesystem::path& path) {
  return std::filesystem::exists(path);
}

void print_issue_summary(const std::unordered_map<std::string, IssueState>& issues) {
  std::size_t shown = 0;
  for (const auto& [issue_code, issue_state] : issues) {
    if (shown >= 10) {
      break;
    }
    std::cout << "Issue " << issue_code << ": " << issue_state.live_orders.size()
              << " live orders, last sequence " << issue_state.last_sequence_number
              << ", last update " << issue_state.last_update_number << "\n";
    ++shown;
  }
}

}  // namespace

int run(const AppConfig& config) {
  for (const auto& pcap_path : config.pcap_paths) {
    if (!exists(pcap_path)) {
      std::cerr << "PCAP file not found: " << pcap_path << "\n";
      return 1;
    }
  }

  if (config.venue_json_path && !exists(*config.venue_json_path)) {
    std::cerr << "Venue JSON file not found: " << *config.venue_json_path << "\n";
    return 1;
  }

  PcapReader pcap_reader;
  NetworkDecoder network_decoder;
  FlexParser flex_parser;
  OrderBookReplayer replayer;

  std::uint64_t total_capture_records = 0;
  std::uint64_t total_udp_datagrams = 0;

  for (const auto& pcap_path : config.pcap_paths) {
    const auto records = pcap_reader.read_all(pcap_path);
    total_capture_records += records.size();

    for (const auto& record : records) {
      const auto datagram = network_decoder.decode_udp(record);
      if (!datagram) {
        continue;
      }
      ++total_udp_datagrams;

      const auto packet = flex_parser.parse(*datagram);
      if (!packet) {
        continue;
      }
      replayer.apply(*packet);
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

  return 0;
}

}  // namespace tse_mbo

