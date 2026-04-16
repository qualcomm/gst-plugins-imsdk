# Add all immediate subdirectories automatically, except excluded ones
file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

set(EXCLUDED_DIRS
    gst-sample-apps-utils
    gst-camera-fd-stream-example
    gst-camera-metadata-example
    gst-camera-per-port-example
    gst-camera-burst-capture-example
    gst-webrtc-sendrecv-example
)

foreach(subdir IN LISTS SUBDIRS)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${subdir}")
        list(FIND EXCLUDED_DIRS "${subdir}" EXCLUDED_INDEX)
        if(EXCLUDED_INDEX EQUAL -1)
            add_subdirectory("${subdir}")
        endif()
    endif()
endforeach()
