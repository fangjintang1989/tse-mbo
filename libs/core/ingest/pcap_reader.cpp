#include "ingest/pcap_reader.hpp"

#include <array>
#include <stdexcept>
#include <string>

#include <zlib.h>

namespace tse_mbo {

namespace {

constexpr std::uint32_t kPcapMagicUsecLe = 0xa1b2c3d4U;
constexpr std::uint32_t kPcapMagicNsecLe = 0xa1b23c4dU;

std::uint32_t read_le_u32(const std::array<unsigned char, 4>& bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void gzread_exact(gzFile file, void* buffer, unsigned int size, const std::string& context) {
  const int read_count = gzread(file, buffer, size);
  if (read_count == 0) {
    throw std::runtime_error("Unexpected EOF while reading " + context);
  }
  if (read_count < 0) {
    int errnum = Z_OK;
    const char* msg = gzerror(file, &errnum);
    throw std::runtime_error("gzread failed while reading " + context + ": " + (msg ? msg : "unknown"));
  }
  if (static_cast<unsigned int>(read_count) != size) {
    throw std::runtime_error("Short read while reading " + context);
  }
}

}  // namespace

std::vector<CaptureRecord> PcapReader::read_all(const std::filesystem::path& path) const {
  gzFile file = gzopen(path.string().c_str(), "rb");
  if (file == nullptr) {
    throw std::runtime_error("Failed to open gzip file: " + path.string());
  }

  try {
    std::array<unsigned char, 24> global_header{};
    gzread_exact(file, global_header.data(), static_cast<unsigned int>(global_header.size()), "pcap global header");

    const std::array<unsigned char, 4> magic_bytes{
        global_header[0], global_header[1], global_header[2], global_header[3]};
    const std::uint32_t magic = read_le_u32(magic_bytes);
    if (magic != kPcapMagicUsecLe && magic != kPcapMagicNsecLe) {
      throw std::runtime_error("Unsupported PCAP magic in file: " + path.string());
    }

    std::vector<CaptureRecord> records;
    while (true) {
      std::array<unsigned char, 16> packet_header{};
      const int header_read = gzread(file, packet_header.data(), static_cast<unsigned int>(packet_header.size()));
      if (header_read == 0) {
        break;
      }
      if (header_read < 0) {
        int errnum = Z_OK;
        const char* msg = gzerror(file, &errnum);
        throw std::runtime_error("Failed while reading packet header: " + std::string(msg ? msg : "unknown"));
      }
      if (header_read != static_cast<int>(packet_header.size())) {
        throw std::runtime_error("Short packet header read in file: " + path.string());
      }

      const std::uint32_t ts_sec =
          static_cast<std::uint32_t>(packet_header[0]) |
          (static_cast<std::uint32_t>(packet_header[1]) << 8U) |
          (static_cast<std::uint32_t>(packet_header[2]) << 16U) |
          (static_cast<std::uint32_t>(packet_header[3]) << 24U);
      const std::uint32_t ts_subsec =
          static_cast<std::uint32_t>(packet_header[4]) |
          (static_cast<std::uint32_t>(packet_header[5]) << 8U) |
          (static_cast<std::uint32_t>(packet_header[6]) << 16U) |
          (static_cast<std::uint32_t>(packet_header[7]) << 24U);
      const std::uint32_t incl_len =
          static_cast<std::uint32_t>(packet_header[8]) |
          (static_cast<std::uint32_t>(packet_header[9]) << 8U) |
          (static_cast<std::uint32_t>(packet_header[10]) << 16U) |
          (static_cast<std::uint32_t>(packet_header[11]) << 24U);

      CaptureRecord record;
      record.ts_sec = ts_sec;
      record.ts_subsec = ts_subsec;
      record.data.resize(incl_len);
      gzread_exact(file, record.data.data(), incl_len, "pcap packet data");
      records.push_back(std::move(record));
    }

    gzclose(file);
    return records;
  } catch (...) {
    gzclose(file);
    throw;
  }
}

}  // namespace tse_mbo

