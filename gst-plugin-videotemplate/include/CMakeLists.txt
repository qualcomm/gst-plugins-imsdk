set(VIDEO_TEMPLATE_DEFS_LIB gstqtivideotemplatedefs)

add_library(${VIDEO_TEMPLATE_DEFS_LIB} INTERFACE)

target_include_directories(${VIDEO_TEMPLATE_DEFS_LIB} INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

