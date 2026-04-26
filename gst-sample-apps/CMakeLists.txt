file(GLOB SAMPLE_APP_DIRS
     RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     gst-* )

foreach(dir ${SAMPLE_APP_DIRS})

  if(NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    continue()
  endif()
  if(dir STREQUAL "gst-sample-apps-utils")
    continue()
  endif()
  if (dir MATCHES "gst-webrtc-sendrecv-example")
    continue()
  endif()
  add_subdirectory(${dir})
endforeach()
