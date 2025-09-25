cmake_minimum_required(VERSION 3.8.2)
project(QTI_OSS_ML_MODULES LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

include_directories(
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_subdirectory(audio-classification)
add_subdirectory(image-classification)
add_subdirectory(image-segmentation)
add_subdirectory(object-detection)
add_subdirectory(pose-estimation)
add_subdirectory(super-resolution)
add_subdirectory(tensor-generation)
