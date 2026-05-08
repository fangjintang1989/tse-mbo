#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "tse_flex_mbo/app_config.hpp"

namespace {

using tse_flex_mbo::AppConfig;

struct ParsedArgs {
  AppConfig config;
  bool help_requested = false;
  bool valid = false;
};

void print_usage() {
  std::cout
      << "Usage:\n"
      << "  tse_flex_mbo_historical_processor"
      << " --pcap <file> [--pcap <file> ...]"
      << " --venue-json <file>"
      << " --output <file>\n";
}

bool file_exists(const std::filesystem::path& path) {
  return std::filesystem::exists(path);
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
      result.config.venue_json_path = *value;
      continue;
    }

    if (arg == "--output") {
      auto value = require_value("--output");
      if (!value) {
        return result;
      }
      result.config.output_csv_path = *value;
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    return result;
  }

  if (result.config.pcap_paths.empty()) {
    std::cerr << "At least one --pcap file is required.\n";
    return result;
  }

  if (result.config.venue_json_path.empty()) {
    std::cerr << "--venue-json is required.\n";
    return result;
  }

  if (result.config.output_csv_path.empty()) {
    std::cerr << "--output is required.\n";
    return result;
  }

  result.valid = true;
  return result;
}

bool validate_inputs(const AppConfig& config) {
  bool ok = true;

  for (const auto& pcap_path : config.pcap_paths) {
    if (!file_exists(pcap_path)) {
      std::cerr << "PCAP file not found: " << pcap_path << "\n";
      ok = false;
    }
  }

  if (!file_exists(config.venue_json_path)) {
    std::cerr << "Venue JSON file not found: " << config.venue_json_path << "\n";
    ok = false;
  }

  return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto parsed = parse_args(argc, argv);
  if (parsed.help_requested) {
    return 0;
  }
  if (!parsed.valid) {
    return argc > 1 ? 1 : 0;
  }

  if (!validate_inputs(parsed.config)) {
    return 1;
  }

  std::cout << "Input validation succeeded.\n";
  std::cout << "PCAP files:\n";
  for (const auto& pcap_path : parsed.config.pcap_paths) {
    std::cout << "  - " << pcap_path << "\n";
  }
  std::cout << "Venue JSON: " << parsed.config.venue_json_path << "\n";
  std::cout << "Output CSV: " << parsed.config.output_csv_path << "\n\n";

  std::cout
      << "The parser and order-book replay engine are not implemented yet.\n"
      << "This scaffold is ready for the next step: PCAP decoding, tag parsing,\n"
      << "and indicative open auction calculation.\n";

  return 0;
}
