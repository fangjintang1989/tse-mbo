cmake_minimum_required(VERSION 3.20)

project(tse_mbo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(ZLIB REQUIRED)

add_library(
  tse_mbo_lib
  src/app/app.cpp
  src/replay/replay_runner.cpp
  src/ingest/network_decoder.cpp
  src/ingest/pcap_reader.cpp
  src/flex/flex_parser.cpp
  src/flex/flex_message.cpp
  src/tse/tse.cpp
  src/book/order_book.cpp
  src/book/indicative.cpp
)

target_include_directories(tse_mbo_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tse_mbo_lib PUBLIC ZLIB::ZLIB)

add_executable(tse_mbo src/cli/main.cpp)
target_link_libraries(tse_mbo PRIVATE tse_mbo_lib)

add_executable(tse_mbo_tests tests/test_main.cpp)
target_link_libraries(tse_mbo_tests PRIVATE tse_mbo_lib)

add_test(NAME tse_mbo_unit_tests COMMAND tse_mbo_tests)

set(TSE_MBO_FIXTURE_PCAP_051 ${CMAKE_CURRENT_SOURCE_DIR}/data/20241105_051.test.pcap.gz)
set(TSE_MBO_FIXTURE_PCAP_052 ${CMAKE_CURRENT_SOURCE_DIR}/data/20241105_052.test.pcap.gz)
set(TSE_MBO_FIXTURE_VENUE_JSON ${CMAKE_CURRENT_SOURCE_DIR}/data/TseVenue.20241105.json)

if(EXISTS ${TSE_MBO_FIXTURE_PCAP_051} AND EXISTS ${TSE_MBO_FIXTURE_PCAP_052} AND EXISTS ${TSE_MBO_FIXTURE_VENUE_JSON})
  add_executable(tse_mbo_fixture_tests tests/fixture_capture_test.cpp)
  target_link_libraries(tse_mbo_fixture_tests PRIVATE tse_mbo_lib)
  target_compile_definitions(
    tse_mbo_fixture_tests
    PRIVATE
      TSE_MBO_FIXTURE_PCAP_051="${TSE_MBO_FIXTURE_PCAP_051}"
      TSE_MBO_FIXTURE_PCAP_052="${TSE_MBO_FIXTURE_PCAP_052}"
      TSE_MBO_FIXTURE_VENUE_JSON="${TSE_MBO_FIXTURE_VENUE_JSON}"
      TSE_MBO_RESULTS_DIR="${CMAKE_CURRENT_BINARY_DIR}/results"
  )

  add_test(NAME tse_mbo_fixture_tests COMMAND tse_mbo_fixture_tests)
endif()

if(MSVC)
  target_compile_options(tse_mbo_lib PRIVATE /W4 /permissive-)
  target_compile_options(tse_mbo PRIVATE /W4 /permissive-)
  target_compile_options(tse_mbo_tests PRIVATE /W4 /permissive-)
  if(TARGET tse_mbo_fixture_tests)
    target_compile_options(tse_mbo_fixture_tests PRIVATE /W4 /permissive-)
  endif()
else()
  target_compile_options(tse_mbo_lib PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tse_mbo PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(tse_mbo_tests PRIVATE -Wall -Wextra -Wpedantic)
  if(TARGET tse_mbo_fixture_tests)
    target_compile_options(tse_mbo_fixture_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()
