project(GST_PLUGIN_QTI_OSS_SEGMENTATION
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-variable")

# GStreamer plugin.
set(GST_HEXAGONNN qtihexagonnn)

add_library(${GST_HEXAGONNN} SHARED
  gsthexagonnn.cc
)

target_include_directories(${GST_HEXAGONNN} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_HEXAGONNN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  qtimlmeta
  Engine_DEEPLABV3
  Engine_POSENET
)

install(
  TARGETS ${GST_HEXAGONNN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
)