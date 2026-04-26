file(GLOB CAMERA_SAMPLE_APPS_DIRS
     RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     *)

foreach(dir ${CAMERA_SAMPLE_APPS_DIRS})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
    add_subdirectory(${dir})
  endif()
endforeach()
