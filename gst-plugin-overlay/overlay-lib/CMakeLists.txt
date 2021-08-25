cmake_minimum_required(VERSION 3.1)

project(overlay_lib)

if (NOT OVERLAY_ENABLED)
set(exclude EXCLUDE_FROM_ALL)
endif()

add_library(overlay_lib SHARED ${exclude}
${CMAKE_CURRENT_SOURCE_DIR}/overlay_lib.cc
)

target_include_directories(overlay_lib
 PRIVATE ${TOP_DIRECTORY})

target_include_directories(overlay_lib
 PUBLIC ${GST_INCLUDE_DIRS})

target_include_directories(overlay_lib
 PRIVATE $<BUILD_INTERFACE:${KERNEL_BUILDDIR}/usr/include>)

# Headers path for msm-5.4 media on qrbx210 targets.
target_include_directories(overlay_lib
 PRIVATE $<BUILD_INTERFACE:${KERNEL_BUILDDIR}/usr/include/vidc>)

# TODO remove this hack when camx issue with propagating c and cpp flags is solved
target_include_directories(overlay_lib
 PRIVATE ${PKG_CONFIG_SYSROOT_DIR}/usr/include/ion_headers)

install(TARGETS overlay_lib DESTINATION lib OPTIONAL)
target_link_libraries(overlay_lib log pthread utils cutils dl C2D2 cairo OpenCL)

# TODO remove this hack when camx issue with propagating c and cpp flags is solved
target_link_libraries(overlay_lib ion)

file(GLOB_RECURSE RGB_FILES ${CMAKE_CURRENT_LIST_DIR}/raw_image/*.rgba)
install(FILES ${RGB_FILES} DESTINATION /data/misc)

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/overlay_blit_rgba_kernel.cl
          ${CMAKE_CURRENT_SOURCE_DIR}/overlay_blit_bgra_kernel.cl
          ${CMAKE_CURRENT_SOURCE_DIR}/overlay_mask_kernel.cl
    DESTINATION /usr/lib
)
