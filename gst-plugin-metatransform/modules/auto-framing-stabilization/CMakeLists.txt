project(qtiafralgo
  LANGUAGES CXX
)

set(AUTO_FRAMING_LIBRARY qtiafralgo)

add_library(${AUTO_FRAMING_LIBRARY} SHARED
  auto-framing-alg.cc
  auto-framing-filters.cc
  auto-framing-basic-movement.cc
  auto-framing-exponent-movement.cc
)

set_target_properties(${AUTO_FRAMING_LIBRARY} PROPERTIES PUBLIC_HEADER
  "include/auto-framing-alg.h"
)

target_include_directories(${AUTO_FRAMING_LIBRARY} PUBLIC
  include
)

target_link_libraries(${AUTO_FRAMING_LIBRARY} PRIVATE
  m
)

install(
  TARGETS ${AUTO_FRAMING_LIBRARY}
  LIBRARY DESTINATION ${INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
