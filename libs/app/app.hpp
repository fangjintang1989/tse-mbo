#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tse_mbo {

struct AppConfig {
  std::vector<std::filesystem::path> pcap_paths;
  std::optional<std::filesystem::path> venue_json_path;
  bool summary_only = false;
};

struct ParsedArgs {
  AppConfig config;
  bool help_requested = false;
  bool valid = false;
};

ParsedArgs parse_args(int argc, char* argv[]);
int run(const AppConfig& config);
void print_usage();

}  // namespace tse_mbo
