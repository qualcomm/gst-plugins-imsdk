cmake_minimum_required(VERSION 3.16)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
# Use in-tree CMake targets if built together, otherwise fall back to pkg-config
if(TARGET gstqtiutilsbase)
  set(GST_QCOM_UTILS_INCLUDE_DIRS "")
  set(GST_QCOM_UTILS_LIBRARIES    gstqtiutilsbase)
else()
  pkg_check_modules(GST_QCOM_UTILS
    REQUIRED gstreamer-qcom-oss-utils-1.0>=1.0.0)
endif()

# GStreamer gst-fastswitch-example
set(GST_EXAMPLE_BIN gst-fastswitch-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${GST_QCOM_UTILS_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  qmmf_camera_metadata
  ${GST_QCOM_UTILS_LIBRARIES}
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
