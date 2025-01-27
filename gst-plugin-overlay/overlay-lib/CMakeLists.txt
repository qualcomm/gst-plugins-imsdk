cmake_minimum_required(VERSION 3.1)

project(overlay_lib)

if (NOT OVERLAY_ENABLED)
set(exclude EXCLUDE_FROM_ALL)
endif()

add_library(overlay_lib SHARED ${exclude}
  ${CMAKE_CURRENT_SOURCE_DIR}/overlay_lib.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/open_cl_funcs.cc
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

target_compile_definitions(overlay_lib PRIVATE
  $<$<BOOL:${HAVE_C2D_H}>:ENABLE_C2D>
  $<$<BOOL:${HAVE_GLES_H}>:ENABLE_GLES>
  $<$<BOOL:${HAVE_MMM_COLOR_FMT_H}>:HAVE_MMM_COLOR_FMT_H>
  $<$<BOOL:${HAVE_MSM_MEDIA_INFO_H}>:HAVE_MSM_MEDIA_INFO_H>
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
  $<$<BOOL:${HAVE_LINUX_DMA_BUF_H}>:HAVE_LINUX_DMA_BUF_H>
)

install(TARGETS overlay_lib DESTINATION lib OPTIONAL)
target_link_libraries(overlay_lib
  dl
  pthread
  cairo
  $<$<BOOL:${HAVE_C2D_H}>:C2D2>
  ${GST_LIBRARIES}
)

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/overlay_blit_rgba_kernel.cl
          ${CMAKE_CURRENT_SOURCE_DIR}/overlay_blit_bgra_kernel.cl
          ${CMAKE_CURRENT_SOURCE_DIR}/overlay_mask_kernel.cl
    DESTINATION /usr/lib
)
