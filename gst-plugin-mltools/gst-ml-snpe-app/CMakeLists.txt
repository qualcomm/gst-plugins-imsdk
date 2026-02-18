# Dynamic loading test

include(CheckIncludeFile)

list(APPEND CMAKE_REQUIRED_INCLUDES
  "${CMAKE_SYSROOT}/${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/SNPE")
check_include_file("DlContainer/DlContainer.h" HAVE_DL_CONTAINER_H)

if(HAVE_DL_CONTAINER_H)
  add_executable(test-dl-snpe test-dl-snpe.cc)

  target_include_directories(test-dl-snpe PUBLIC
    ${CMAKE_SYSROOT}/${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/SNPE
    ${GST_INCLUDE_DIRS}
  )

  target_link_libraries(test-dl-snpe PRIVATE
    ${CMAKE_DL_LIBS}
  )

  install(
    TARGETS test-dl-snpe
    RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ
  )
endif()
