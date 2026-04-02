cmake_minimum_required(VERSION 3.16)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Include directories
include_directories(${SYSROOT_INCDIR})

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

add_library(genie-vlm SHARED
  GenieVLM.cc
)

install(
  TARGETS genie-vlm
  LIBRARY DESTINATION ${INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
