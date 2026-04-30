set(GST_CAMERA_SAMPLE_APPS
    gst-camera-fd-stream-example
    gst-camera-per-port-example
    gst-camera-burst-capture-example
    gst-camera-metadata-example
)

message(STATUS "ENABLE_GST_SAMPLE_APPS_CAMERA = ${ENABLE_GST_SAMPLE_APPS_CAMERA}")
if (ENABLE_GST_SAMPLE_APPS_CAMERA)
  foreach(dir ${GST_CAMERA_SAMPLE_APPS})
    if (IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
      message(STATUS "Directory exists, adding subdirectory: ${dir}")
      add_subdirectory(${dir})
    else()
      message(STATUS "Directory does NOT exist: ${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    endif()
  endforeach()
  else()
  message(STATUS "ENABLE_GST_SAMPLE_APPS_CAMERA is disabled")
endif()

if (ENABLE_GST_SAMPLE_APPS)
  file(GLOB GST_SAMPLE_APPS_DIRS
       RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
       gst-* )

  foreach(dir ${GST_SAMPLE_APPS_DIRS})

    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
      continue()
    endif()

    list(FIND GST_CAMERA_SAMPLE_APPS ${dir} _camera_index)
    if (_camera_index GREATER -1)
      continue()
    endif()

    if (dir STREQUAL "gst-sample-apps-utils")
      continue()
    endif()

    if (dir MATCHES "gst-webrtc-sendrecv-example")
      continue()
    endif()

    add_subdirectory(${dir})

  endforeach()
endif()
