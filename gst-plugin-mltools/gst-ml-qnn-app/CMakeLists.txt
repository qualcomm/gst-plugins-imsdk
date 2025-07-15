# Dynamic loading test

include(CheckIncludeFile)

check_include_file("System/QnnSystemInterface.h" HAVE_QNN_SYSTEM_INTERFACE_H)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

if(HAVE_QNN_SYSTEM_INTERFACE_H)
  add_executable(test-dl-qnn test-dl-qnn.cc)

  target_include_directories(test-dl-qnn PUBLIC
    ${GST_INCLUDE_DIRS}
  )

  target_link_libraries(test-dl-qnn PRIVATE
    ${CMAKE_DL_LIBS}
  )

  install(
    TARGETS test-dl-qnn
    RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ
  )

endif()
