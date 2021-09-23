cmake_minimum_required(VERSION 3.8.2)

if (NOT CMAKE_CROSSCOMPILING)
set (GST_VERSION_REQUIRED 1.14.4)
set (GST_PLUGINS_QTI_OSS_PACKAGE "gst-plugin-streamdemux")
set (GST_PLUGINS_QTI_OSS_VERSION 1.0)
set (GST_PLUGINS_QTI_OSS_LICENSE "BSD")
set (GST_PLUGINS_QTI_OSS_SUMMARY "QTI open-source GStreamer Plug-in for streamdemux")
set (GST_PLUGINS_QTI_OSS_ORIGIN "https://www.codeaurora.org/")
set (GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR /usr/lib/aarch64-linux-gnu/gstreamer-1.0/)
endif()

project(GST_PLUGIN_QTI_OSS_STREAMDEMUX
    VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
    LANGUAGES C
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3")

set(GST_QTI_STREAMDEMUX gstqtistreamdemux)

add_library(gstqtistreamdemux SHARED gststreamdemux.c)

target_include_directories(${GST_QTI_STREAMDEMUX} PUBLIC ${GST_INCLUDE_DIRS})

target_link_libraries(${GST_QTI_STREAMDEMUX} PRIVATE ${GST_LIBRARIES} ${GST_VIDEO_LIBRARIES})

install(
    TARGETS ${GST_QTI_STREAMDEMUX}
    LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ
)