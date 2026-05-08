#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace tse_mbo {

struct CaptureRecord {
  std::uint32_t ts_sec = 0;
  std::uint32_t ts_subsec = 0;
  std::vector<std::byte> data;
};

class PcapReader {
 public:
  std::vector<CaptureRecord> read_all(const std::filesystem::path& path) const;
};

}  // namespace tse_mbo

