cmake_minimum_required(VERSION 3.16)

project(genie-vlm
  VERSION 1.0.0
  LANGUAGES C CXX
  DESCRIPTION "Genie wrapper library"
)

set(TARGET_NAME genie-vlm)

set(TARGET_HEADERS
  GenieVLM.h
)

# Include directories
include_directories(${SYSROOT_INCDIR})

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

add_library(${TARGET_NAME} SHARED
   GenieVLM.cc
)

set_target_properties(${TARGET_NAME} PROPERTIES
  PUBLIC_HEADER "${TARGET_HEADERS}"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)


install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
