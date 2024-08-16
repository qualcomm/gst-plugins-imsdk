# GStreamer QTI UTILS base library.
set(GST_QTI_UTILS_BASE gstqtiutilsbase)

if (ENABLE_RUNTIME_PARSER)
  add_definitions(-DENABLE_RUNTIME_PARSER)

  pkg_check_modules(GST_JSON
    REQUIRED json-glib-1.0)

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")

  list(APPEND RUNTIME_FLAGS_PARSER_SRCS
    runtime-flags-parser.cc
    runtime-flags-parser-c-api.cc
  )

  set(RUNTIME_FLAGS_PARSER_HEADERS
    "runtime-flags-parser.h\;runtime-flags-parser-c-api.h\;"
  )

  list(APPEND RUNTIME_FLAGS_PARSER_TARGET_LIBRARIES
    ${GST_JSON_LIBRARIES}
  )

  list(APPEND RUNTIME_FLAGS_PARSER_TARGET_INCLUDES
    ${GST_JSON_INCLUDE_DIRS}
  )

endif()

add_library(${GST_QTI_UTILS_BASE} SHARED
  common-utils.c
  batch-utils.c
  ${RUNTIME_FLAGS_PARSER_SRCS}
)

set(DEFAULT_PUBLIC_HEADERS "common-utils.h\;batch-utils.h\;${RUNTIME_FLAGS_PARSER_HEADERS}")

set_target_properties(${GST_QTI_UTILS_BASE} PROPERTIES PUBLIC_HEADER
  ${DEFAULT_PUBLIC_HEADERS}
)

target_include_directories(${GST_QTI_UTILS_BASE} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${RUNTIME_FLAGS_PARSER_TARGET_INCLUDES}
)

target_link_libraries(${GST_QTI_UTILS_BASE} PRIVATE
  ${GST_LIBRARIES}
  ${RUNTIME_FLAGS_PARSER_TARGET_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_UTILS_BASE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/utils/
)
