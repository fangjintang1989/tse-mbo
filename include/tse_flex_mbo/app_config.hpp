#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tse_flex_mbo {

struct AppConfig {
  std::vector<std::filesystem::path> pcap_paths;
  std::filesystem::path venue_json_path;
  std::filesystem::path output_csv_path;
};

}  // namespace tse_flex_mbo

