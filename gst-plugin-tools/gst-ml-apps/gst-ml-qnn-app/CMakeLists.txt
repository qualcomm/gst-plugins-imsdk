# Dynamic loading test

include(CheckIncludeFile)

check_include_file("System/QnnSystemInterface.h" HAVE_QNN_SYSTEM_INTERFACE_H)

if(HAVE_QNN_SYSTEM_INTERFACE_H)
  add_executable(test-dl-qnn test-dl-qnn.cc)

  target_link_libraries(test-dl-qnn PRIVATE
    ${CMAKE_DL_LIBS}
  )

  install(
    TARGETS test-dl-qnn
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ
  )

endif()
