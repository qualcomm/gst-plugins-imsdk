cmake_minimum_required(VERSION 3.8.2)
project(QTI_OSS_HEXAGON_MODULES LANGUAGES C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_subdirectory(ubwcdma)
