# Add all immediate subdirectories automatically
file(GLOB SUBDIRS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" *)

set(CAMERA_APP_DIRS
    gst-camera-fd-stream-example
    gst-camera-metadata-example
    gst-camera-per-port-example
    gst-camera-burst-capture-example
)

foreach(subdir IN LISTS SUBDIRS)
  if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${subdir}")
    continue()
  endif()

  if(subdir STREQUAL "gst-sample-apps-utils")
    continue()
  endif()

  if(NOT ENABLE_GST_PLUGIN_CAMERA_BASE AND subdir IN_LIST CAMERA_APP_DIRS)
    continue()
  endif()

  add_subdirectory("${subdir}")
endforeach()
