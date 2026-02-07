set(GST_QTI_OBJTRACKER_BYTETRACK  objtracker-bytetrack)

set(LIB_NAME ${GST_QTI_OBJTRACKER_BYTETRACK})
set(LIB_DESCRIPTION "GStreamer QTI Bytetrack Algorithm library")

find_package(PkgConfig REQUIRED)
pkg_check_modules(EIGEN3 REQUIRED eigen3)

add_library(${GST_QTI_OBJTRACKER_BYTETRACK} SHARED
  bytetrack-objtracker.cc
  BYTETracker.cpp
  utils.cpp
  lapjv.cpp
  STrack.cpp
  kalmanFilter.cpp
)

target_include_directories(${GST_QTI_OBJTRACKER_BYTETRACK} PRIVATE
  ${EIGEN3_INCLUDE_DIRS}
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_OBJTRACKER_BYTETRACK} PRIVATE
  ${GST_QTI_OBJTRACKER_ALGO}
)

install(
  TARGETS ${GST_QTI_OBJTRACKER_BYTETRACK}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/imsdk/qtiobjtracker/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
