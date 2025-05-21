include(CheckIncludeFile)
include(CheckIncludeFileCXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Prefer the prebuilt TFLite package.
pkg_check_modules(TFLITE tensorflow-lite-prebuilt)
if(NOT TFLITE_FOUND)
  pkg_check_modules(TFLITE tensorflow-lite)
endif()

# Check whether the C API header for the TFLITE library is present.
check_include_file("tensorflow/lite/core/shims/c/c_api.h" HAVE_CORE_SHIMS_C_API_H)
check_include_file("tensorflow/lite/core/c/c_api.h" HAVE_CORE_C_API_H)
check_include_file("tensorflow/lite/c/c_api.h" HAVE_C_API_H)

list(APPEND TARGET_COMPILE_DEFINITIONS
  $<$<BOOL:${HAVE_C_API_H}>:HAVE_C_API_H>
  $<$<BOOL:${HAVE_CORE_C_API_H}>:HAVE_CORE_C_API_H>
  $<$<BOOL:${HAVE_CORE_SHIMS_C_API_H}>:HAVE_CORE_SHIMS_C_API_H>
)

list(APPEND TARGET_INCS
  $<$<VERSION_LESS_EQUAL:${TFLITE_VERSION},2.2.0>:${SYSROOT_INCDIR}/tflite>
  $<$<VERSION_GREATER:${TFLITE_VERSION},2.2.0>:${SYSROOT_INCDIR}/tensorflow>
)

list(APPEND TARGET_LIBRARIES
  ${CMAKE_DL_LIBS}
)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")

if(TFLITE_FOUND)
  add_executable(test-dl-tflite test-dl-tflite.cc)

  target_compile_definitions(test-dl-tflite PRIVATE
    ${TARGET_COMPILE_DEFINITIONS}
  )

  target_include_directories(test-dl-tflite PUBLIC
    ${TARGET_INCS}
  )

  target_link_libraries(test-dl-tflite PRIVATE
    ${TARGET_LIBRARIES}
  )

  install(
    TARGETS test-dl-tflite
    RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  )
endif()
