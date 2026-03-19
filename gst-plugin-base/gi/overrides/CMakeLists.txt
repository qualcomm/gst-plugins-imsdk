if(Python3_FOUND AND PYGOBJECT_FOUND)
  set(TARGET_NAME _gi_gst_qti)

  Python3_add_library(${TARGET_NAME} MODULE
    gstmodule.c
  )

  target_include_directories(${TARGET_NAME} PRIVATE
    ${PYGOBJECT_INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
  )

  target_include_directories(${TARGET_NAME} INTERFACE
    $<BUILD_INTERFACE:${QTI_PLUGINS_BASE_SOURCE_DIR}>
  )

  target_link_libraries(${TARGET_NAME} PRIVATE
    ${GLIB_LIBRARIES}
    ${PYGOBJECT_LIBRARIES}
    gstqtimlbase
  )

  install(
    TARGETS ${TARGET_NAME}
    LIBRARY DESTINATION ${PYTHON_SITEPACKAGES_DIR}/gi/overrides
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ
  )

  # Install the overrides so 'from gi.repository import' picks them up.
  install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/GstQtiML.py
    DESTINATION ${PYTHON_SITEPACKAGES_DIR}/gi/overrides
  )
endif()
