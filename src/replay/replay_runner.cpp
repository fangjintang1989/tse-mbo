#include "replay/replay_runner.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "flex/flex_message.hpp"
#include "flex/flex_parser.hpp"
#include "ingest/network_decoder.hpp"
#include "ingest/pcap_reader.hpp"

namespace tse_mbo {

namespace {

struct ReplayRecord {
  CaptureRecord record;
  std::size_t file_index = 0;
  std::size_t record_index = 0;
};

void replay_packet_payloads(const UdpDatagramView& datagram,
                            const FlexParser& flex_parser,
                            ReplayDataCallback& callback) {
  const auto packets = flex_parser.parse_all(datagram);
  for (const auto& packet : packets) {
    callback.on_flex_packet(normalize_flex_packet(packet));
  }
}

bool replay_capture_record(const CaptureRecord& record,
                           const NetworkDecoder& network_decoder,
                           const FlexParser& flex_parser,
                           ReplayDataCallback& callback) {
  const auto datagram = network_decoder.decode_udp(record);
  if (!datagram) {
    return false;
  }

  replay_packet_payloads(*datagram, flex_parser, callback);
  return true;
}

void replay_by_timestamp(const std::vector<std::filesystem::path>& pcap_paths,
                         const PcapReader& pcap_reader,
                         const NetworkDecoder& network_decoder,
                         const FlexParser& flex_parser,
                         ReplayDataCallback& callback,
                         ReplaySummary& summary) {
  std::vector<ReplayRecord> merged_records;
  for (std::size_t file_index = 0; file_index < pcap_paths.size(); ++file_index) {
    auto records = pcap_reader.read_all(pcap_paths[file_index]);
    summary.capture_records += records.size();
    merged_records.reserve(merged_records.size() + records.size());
    for (std::size_t record_index = 0; record_index < records.size(); ++record_index) {
      merged_records.push_back(ReplayRecord{
          .record = std::move(records[record_index]),
          .file_index = file_index,
          .record_index = record_index,
      });
    }
  }

  std::stable_sort(merged_records.begin(), merged_records.end(),
                   [](const ReplayRecord& lhs, const ReplayRecord& rhs) {
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

  for (const auto& replay_record_view : merged_records) {
    if (replay_capture_record(replay_record_view.record, network_decoder, flex_parser, callback)) {
      ++summary.udp_datagrams;
    }
  }
}

}  // namespace

ReplaySummary replay_pcaps(const std::vector<std::filesystem::path>& pcap_paths,
                           ReplayDataCallback& callback) {
  PcapReader pcap_reader;
  NetworkDecoder network_decoder;
  FlexParser flex_parser;
  ReplaySummary summary;

  replay_by_timestamp(pcap_paths, pcap_reader, network_decoder, flex_parser, callback, summary);

  return summary;
}

}  // namespace tse_mbo
