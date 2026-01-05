# Add all immediate subdirectories automatically
file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

foreach(subdir IN LISTS SUBDIRS)
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${subdir}
        AND NOT subdir STREQUAL "gst-sample-apps-utils")
      add_subdirectory(${subdir})
  endif()
endforeach()
