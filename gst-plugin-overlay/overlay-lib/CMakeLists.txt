cmake_minimum_required(VERSION 3.1)

project(overlay_lib)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_SKIA=0 -DUSE_CAIRO=1")

if (NOT OVERLAY_ENABLED)
set(exclude EXCLUDE_FROM_ALL)
endif()

add_definitions(-DOVERLAY_OPEN_CL_BLIT)

add_library(overlay_lib SHARED ${exclude}
${CMAKE_CURRENT_SOURCE_DIR}/overlay_lib.cc
)

target_include_directories(overlay_lib
 PRIVATE ${TOP_DIRECTORY})

target_include_directories(overlay_lib
 PUBLIC ${GST_INCLUDE_DIRS})

target_include_directories(overlay_lib
 PRIVATE $<BUILD_INTERFACE:${KERNEL_BUILDDIR}/usr/include>)

# TODO remove this hack when camx issue with propagating c and cpp flags is solved
target_include_directories(overlay_lib
 PRIVATE ${PKG_CONFIG_SYSROOT_DIR}/usr/include/ion_headers)

install(TARGETS overlay_lib DESTINATION lib OPTIONAL)
target_link_libraries(overlay_lib log pthread utils cutils dl C2D2 cairo OpenCL)

# TODO remove this hack when camx issue with propagating c and cpp flags is solved
target_link_libraries(overlay_lib ion)

file(GLOB_RECURSE RGB_FILES ${CMAKE_CURRENT_LIST_DIR}/raw_image/*.rgba)
install(FILES ${RGB_FILES} DESTINATION ${QMMF_DATA})

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/overlay_blit_kernel.cl
    DESTINATION /usr/lib/qmmf
)
