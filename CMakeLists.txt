cmake_minimum_required(VERSION 3.20)

project(
  tse_flex_mbo_historical_processor
  VERSION 0.1.0
  DESCRIPTION "Historical TSE FLEX Full MBO PCAP processor"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(
  tse_flex_mbo_historical_processor
  src/main.cpp
  src/order_book.cpp
)

target_include_directories(
  tse_flex_mbo_historical_processor
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
  target_compile_options(tse_flex_mbo_historical_processor PRIVATE /W4 /permissive-)
else()
  target_compile_options(tse_flex_mbo_historical_processor PRIVATE -Wall -Wextra -Wpedantic)
endif()

