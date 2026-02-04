cmake_minimum_required(VERSION 3.16)
project(gst-plugins-qti-oss LANGUAGES C CXX)
set(GST_VERSION_REQUIRED "1.16")

# Common point for setting version of all libs of gst-plugins-qcom package
set(GST_PLUGINS_QTI_OSS_VERSION "1.0")

set(GST_PLUGINS_QTI_OSS_INSTALL_BINDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
set(GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
set(GST_PLUGINS_QTI_OSS_INSTALL_INCDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}")
set(GST_PLUGINS_QTI_OSS_INSTALL_CONFIG "${CMAKE_INSTALL_SYSCONFDIR}/configs")

option(ENABLE_GST_IMSDK_PLUGINS "Enable IMSDK Plugins" OFF)

option(ENABLE_GST_CAMERA_PLUGINS "Enable Camera Plugins" OFF) # not enabled in opensource mainline
option(ENABLE_GST_EXT_SOFTWARE_PLUGINS "Enable Software plugins depends on external software" ${ENABLE_GST_IMSDK_PLUGINS})
option(ENABLE_GST_MESSAGING_PLUGINS "Enable Messaging plugins" ${ENABLE_GST_IMSDK_PLUGINS})
option(ENABLE_GST_ML_PLUGINS "Enable ML Plugins" ${ENABLE_GST_IMSDK_PLUGINS})
option(ENABLE_GST_VIDEOPROC_PLUGINS "Enable Video processing plugins" ${ENABLE_GST_IMSDK_PLUGINS})
option(ENABLE_GST_SOFTWARE_PLUGINS "Enable Software plugins" ${ENABLE_GST_IMSDK_PLUGINS})

# Camera plugins
option(ENABLE_GST_PLUGIN_CAMIMGREPROC "Enable gst-plugin-camimgreproc" ${ENABLE_GST_CAMERA_PLUGINS})
option(ENABLE_GST_PLUGIN_CAMREPROC "Enable gst-plugin-camreproc" ${ENABLE_GST_CAMERA_PLUGINS})
option(ENABLE_GST_PLUGIN_JPEGENC "Enable gst-plugin-jpegenc" ${ENABLE_GST_CAMERA_PLUGINS})
option(ENABLE_GST_PLUGIN_QMMFSRC "Enable gst-plugin-qmmfsrc" ${ENABLE_GST_CAMERA_PLUGINS})

# External Software Plugins
option(ENABLE_GST_PLUGIN_REDISSINK "Enable gst-plugin-redissink" ${ENABLE_GST_EXT_SOFTWARE_PLUGINS})

# Messaging Plugins
option(ENABLE_GST_PLUGIN_MSGBROKER "Enable gst-plugin-msgbroker" ${ENABLE_GST_MESSAGING_PLUGINS})

# ML Plugins
option(ENABLE_GST_PLUGIN_METAMUX "Enable gst-plugin-metamux" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_METATRANSFORM "Enable gst-plugin-metatransform" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLACONVERTER "Enable gst-plugin-mlaconverter" OFF) # not enabled in opensource mainline
option(ENABLE_GST_PLUGIN_MLDEMUX "Enable gst-plugin-mldemux" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLMETAEXTRACTOR "Enable gst-plugin-mlmetaextractor" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLMETAPARSER "Enable gst-plugin-mlmetaparser" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLPOSTPROCESS "Enable gst-plugin-mlpostprocess" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLQNN "Enable gst-plugin-mlqnn" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLSNPE "Enable gst-plugin-mlsnpe" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLTFLITE "Enable gst-plugin-mltflite" OFF) # not enabled in opensource mainline
option(ENABLE_GST_PLUGIN_MLTOOLS "Enable gst-plugin-mltools" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_MLVCONVERTER "Enable gst-plugin-mlvconverter" ${ENABLE_GST_ML_PLUGINS})
option(ENABLE_GST_PLUGIN_OBJTRACKER "Enable gst-plugin-objtracker" OFF) # not enabled in opensource mainline
option(ENABLE_GST_PLUGIN_RESTRICTED_ZONE "Enable gst-plugin-restricted-zone" ${ENABLE_GST_ML_PLUGINS})

# Video Processing plugins
option(ENABLE_GST_PLUGIN_VCOMPOSER "Enable gst-plugin-vcomposer" ${ENABLE_GST_VIDEOPROC_PLUGINS})
option(ENABLE_GST_PLUGIN_VOVERLAY "Enable gst-plugin-voverlay" ${ENABLE_GST_VIDEOPROC_PLUGINS})
option(ENABLE_GST_PLUGIN_VSPLIT "Enable gst-plugin-vsplit" ${ENABLE_GST_VIDEOPROC_PLUGINS})
option(ENABLE_GST_PLUGIN_VTRANSFORM "Enable gst-plugin-vtransform" ${ENABLE_GST_VIDEOPROC_PLUGINS})

# Software Plugins
option(ENABLE_GST_PLUGIN_BATCH "Enable gst-plugin-batch" ${ENABLE_GST_SOFTWARE_PLUGINS})
option(ENABLE_GST_PLUGIN_RTSPBIN "Enable gst-plugin-rtspbin" ${ENABLE_GST_SOFTWARE_PLUGINS})
option(ENABLE_GST_PLUGIN_SMARTVENCBIN "Enable gst-plugin-smartvencbin" ${ENABLE_GST_SOFTWARE_PLUGINS})
option(ENABLE_GST_PLUGIN_SOCKET "Enable gst-plugin-socket" ${ENABLE_GST_SOFTWARE_PLUGINS})
option(ENABLE_GST_PLUGIN_VIDEOTEMPLATE "Enable gst-plugin-videotemplate" ${ENABLE_GST_SOFTWARE_PLUGINS})

# Sample Apps and Test Framework
option(ENABLE_GST_PLUGIN_EXAMPLES "Enable gst-plugin-examples" OFF)
option(ENABLE_GST_PYTHON_EXAMPLES "Enable gst-python-examples" OFF)
option(ENABLE_GST_SAMPLE_APPS "Enable gst-sample-apps" OFF)
option(ENABLE_GST_PLUGIN_TOOLS "Enable gst-plugin-tools" OFF)
option(ENABLE_GST_TEST_FRAMEWORK "Enable gst-test-framework" OFF)
option(ENABLE_GST_UMD_DAEMON "Enable gst-umd-daemon" OFF)

# Old ML Plugins
option(ENABLE_GST_PLUGIN_MLACLASSIFICATION "Enable gst-plugin-mlaclassification" OFF)
option(ENABLE_GST_PLUGIN_MLVCLASSIFICATION "Enable gst-plugin-mlvclassification" OFF)
option(ENABLE_GST_PLUGIN_MLVDETECTION "Enable gst-plugin-mlvdetection" OFF)
option(ENABLE_GST_PLUGIN_MLVPOSE "Enable gst-plugin-mlvpose" OFF)
option(ENABLE_GST_PLUGIN_MLVSEGMENTATION "Enable gst-plugin-mlvsegmentation" OFF)
option(ENABLE_GST_PLUGIN_MLVSUPERRESOLUTION "Enable gst-plugin-mlvsuperresolution" OFF)

# ACK SP Plugins
option(ENABLE_GST_PLUGIN_CODEC2 "Enable gst-plugin-codec2" OFF)
option(ENABLE_GST_PLUGIN_CV_IMGPIRAMID "Enable gst-plugin-cv-imgpyramid" OFF)
option(ENABLE_GST_PLUGIN_CV_OPTCLFLOW "Enable gst-plugin-cv-optclflow" OFF)
option(ENABLE_GST_PLUGIN_DFS "Enable gst-plugin-dfs" OFF)
option(ENABLE_GST_PLUGIN_DNGPACKER "Enable gst-plugin-dngpacker" OFF)
option(ENABLE_GST_PLUGIN_DRMDECRYPTOR "Enable gst-plugin-drmdecryptor" OFF)
option(ENABLE_GST_PLUGIN_HEIFMUX "Enable gst-plugin-heifmux" OFF)
option(ENABLE_GST_PLUGIN_HEXAGON "Enable gst-plugin-hexagon" OFF)
option(ENABLE_GST_PLUGIN_JPEGPACKER "Enable gst-plugin-jpegpacker" OFF)
option(ENABLE_GST_PLUGIN_MLAIC "Enable gst-plugin-mlaic" OFF)
option(ENABLE_GST_PLUGIN_OVERLAY "Enable gst-plugin-overlay" OFF)

# === Enable exclude from all for internal dependncies ===
set(ENABLE_EXCLUDE_FROM_ALL "EXCLUDE_FROM_ALL")

# Base is common for majority of the plugins, hence enabled by default
add_subdirectory(gst-plugin-base)

# Helper function to add sub-dir when option set
function(add_plugin_if option_var plugin_dir)
  if(DEFINED ${option_var} AND ${option_var})
    message(STATUS "Enabling plugin from ${plugin_dir}")
    add_subdirectory(${plugin_dir})
  endif()
endfunction()

# Camera plugins
add_plugin_if(ENABLE_GST_PLUGIN_CAMIMGREPROC    gst-plugin-camimgreproc)
add_plugin_if(ENABLE_GST_PLUGIN_CAMREPROC       gst-plugin-camreproc)
add_plugin_if(ENABLE_GST_PLUGIN_JPEGENC         gst-plugin-jpegenc)
add_plugin_if(ENABLE_GST_PLUGIN_QMMFSRC         gst-plugin-qmmfsrc)

# External Software Plugins
add_plugin_if(ENABLE_GST_PLUGIN_REDISSINK       gst-plugin-redissink)

# Messaging Plugins
add_plugin_if(ENABLE_GST_PLUGIN_MSGBROKER       gst-plugin-msgbroker)

# ML Plugins
add_plugin_if(ENABLE_GST_PLUGIN_METAMUX         gst-plugin-metamux)
add_plugin_if(ENABLE_GST_PLUGIN_METATRANSFORM   gst-plugin-metatransform)
add_plugin_if(ENABLE_GST_PLUGIN_MLACONVERTER    gst-plugin-mlaconverter)
add_plugin_if(ENABLE_GST_PLUGIN_MLDEMUX         gst-plugin-mldemux)
add_plugin_if(ENABLE_GST_PLUGIN_MLMETAEXTRACTOR gst-plugin-mlmetaextractor)
add_plugin_if(ENABLE_GST_PLUGIN_MLMETAPARSER    gst-plugin-mlmetaparser)
add_plugin_if(ENABLE_GST_PLUGIN_MLPOSTPROCESS   gst-plugin-mlpostprocess)
add_plugin_if(ENABLE_GST_PLUGIN_MLQNN           gst-plugin-mlqnn)
add_plugin_if(ENABLE_GST_PLUGIN_MLSNPE          gst-plugin-mlsnpe)
add_plugin_if(ENABLE_GST_PLUGIN_MLTFLITE        gst-plugin-mltflite)
add_plugin_if(ENABLE_GST_PLUGIN_MLTOOLS         gst-plugin-mltools)
add_plugin_if(ENABLE_GST_PLUGIN_MLVCONVERTER    gst-plugin-mlvconverter)
add_plugin_if(ENABLE_GST_PLUGIN_OBJTRACKER      gst-plugin-objtracker)
add_plugin_if(ENABLE_GST_PLUGIN_RESTRICTED_ZONE gst-plugin-restricted-zone)

# Video Processing plugins
add_plugin_if(ENABLE_GST_PLUGIN_VCOMPOSER       gst-plugin-vcomposer)
add_plugin_if(ENABLE_GST_PLUGIN_VOVERLAY        gst-plugin-voverlay)
add_plugin_if(ENABLE_GST_PLUGIN_VSPLIT          gst-plugin-vsplit)
add_plugin_if(ENABLE_GST_PLUGIN_VTRANSFORM      gst-plugin-vtransform)

# Software Plugins
add_plugin_if(ENABLE_GST_PLUGIN_BATCH           gst-plugin-batch)
add_plugin_if(ENABLE_GST_PLUGIN_RTSPBIN         gst-plugin-rtspbin)
add_plugin_if(ENABLE_GST_PLUGIN_SMARTVENCBIN    gst-plugin-smartvencbin)
add_plugin_if(ENABLE_GST_PLUGIN_SOCKET          gst-plugin-socket)
add_plugin_if(ENABLE_GST_PLUGIN_VIDEOTEMPLATE   gst-plugin-videotemplate)

# Sample Apps and Test Framework
add_plugin_if(ENABLE_GST_PLUGIN_EXAMPLES        gst-plugin-examples)
add_plugin_if(ENABLE_GST_PYTHON_EXAMPLES        gst-python-examples)
add_plugin_if(ENABLE_GST_SAMPLE_APPS            gst-sample-apps)
add_plugin_if(ENABLE_GST_SAMPLE_APPS            gst-sample-apps/gst-sample-apps-utils)
add_plugin_if(ENABLE_GST_PLUGIN_TOOLS           gst-plugin-tools)
add_plugin_if(ENABLE_GST_TEST_FRAMEWORK         gst-test-framework)
add_plugin_if(ENABLE_GST_UMD_DAEMON             gst-umd-daemon)

# Old ML Plugins
add_plugin_if(ENABLE_GST_PLUGIN_MLACLASSIFICATION   gst-plugin-mlaclassification)
add_plugin_if(ENABLE_GST_PLUGIN_MLVCLASSIFICATION   gst-plugin-mlvclassification)
add_plugin_if(ENABLE_GST_PLUGIN_MLVDETECTION        gst-plugin-mlvdetection)
add_plugin_if(ENABLE_GST_PLUGIN_MLVPOSE             gst-plugin-mlvpose)
add_plugin_if(ENABLE_GST_PLUGIN_MLVSEGMENTATION     gst-plugin-mlvsegmentation)
add_plugin_if(ENABLE_GST_PLUGIN_MLVSUPERRESOLUTION  gst-plugin-mlvsuperresolution)

# ACK SP Plugins
add_plugin_if(ENABLE_GST_PLUGIN_CODEC2          gst-plugin-codec2)
add_plugin_if(ENABLE_GST_PLUGIN_CV_IMGPIRAMID   gst-plugin-cv-imgpyramid)
add_plugin_if(ENABLE_GST_PLUGIN_CV_OPTCLFLOW    gst-plugin-cv-optclflow)
add_plugin_if(ENABLE_GST_PLUGIN_DFS             gst-plugin-dfs)
add_plugin_if(ENABLE_GST_PLUGIN_DNGPACKER       gst-plugin-dngpacker)
add_plugin_if(ENABLE_GST_PLUGIN_DRMDECRYPTOR    gst-plugin-drmdecryptor)
add_plugin_if(ENABLE_GST_PLUGIN_HEIFMUX         gst-plugin-heifmux)
add_plugin_if(ENABLE_GST_PLUGIN_HEXAGON         gst-plugin-hexagon)
add_plugin_if(ENABLE_GST_PLUGIN_JPEGPACKER      gst-plugin-jpegpacker)
add_plugin_if(ENABLE_GST_PLUGIN_MLAIC           gst-plugin-mlaic)
add_plugin_if(ENABLE_GST_PLUGIN_OVERLAY         gst-plugin-overlay)
