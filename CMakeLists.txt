cmake_minimum_required(VERSION 3.16)
project(gst-plugins-qti-oss LANGUAGES C CXX)
set(GST_VERSION_REQUIRED "1.16")

# Common point for setting version of all libs of gst-plugins-qcom package
set(GST_PLUGINS_QTI_OSS_VERSION "1.0")

# === Core Plugins ===
option(ENABLE_GST_PLUGIN_QMMFSRC "Enable gst-plugin-qmmfsrc" OFF)
option(ENABLE_GST_PLUGIN_VCOMPOSER "Enable gst-plugin-vcomposer" OFF)
option(ENABLE_GST_PLUGIN_BATCH "Enable gst-plugin-batch" OFF)
option(ENABLE_GST_PLUGIN_METAMUX "Enable gst-plugin-metamux" OFF)
option(ENABLE_GST_PLUGIN_SOCKET "Enable gst-plugin-socket" OFF)
option(ENABLE_GST_PLUGIN_VSPLIT "Enable gst-plugin-vsplit" OFF)
option(ENABLE_GST_PLUGIN_VTRANSFORM "Enable gst-plugin-vtransform" OFF)
option(ENABLE_GST_PLUGIN_VOVERLAY "Enable gst-plugin-voverlay" OFF)
option(ENABLE_GST_PLUGIN_OVERLAY "Enable gst-plugin-overlay" OFF)
option(ENABLE_GST_PLUGIN_RESTRICTED_ZONE "Enable gst-plugin-restricted-zone" OFF)
option(ENABLE_GST_PLUGIN_RTSPBIN "Enable gst-plugin-rtspbin" OFF)
option(ENABLE_GST_PLUGIN_REDISSINK "Enable gst-plugin-redissink" OFF)
option(ENABLE_GST_PLUGIN_SMARTVENCBIN "Enable gst-plugin-smartvencbin" OFF)
option(ENABLE_GST_PLUGIN_CAMIMGREPROC "Enable gst-plugin-camimgreproc" OFF)
option(ENABLE_GST_PLUGIN_CAMREPROC "Enable gst-plugin-camreproc" OFF)
option(ENABLE_GST_PLUGIN_JPEGENC "Enable gst-plugin-jpegenc" OFF)
option(ENABLE_GST_PLUGIN_JPEGPACKER "Enable gst-plugin-jpegpacker" OFF)
option(ENABLE_GST_PLUGIN_VIDEOTEMPLATE "Enable gst-plugin-videotemplate" OFF)
option(ENABLE_GST_PLUGIN_HEIFMUX "Enable gst-plugin-heifmux" OFF)

# === ML Plugins ===
option(ENABLE_GST_PLUGIN_MLACONVERTER "Enable gst-plugin-mlaconverter" OFF)
option(ENABLE_GST_PLUGIN_MLACLASSIFICATION "Enable gst-plugin-mlaclassification" OFF)
option(ENABLE_GST_PLUGIN_MLDEMUX "Enable gst-plugin-mldemux" OFF)
option(ENABLE_GST_PLUGIN_MLVCONVERTER "Enable gst-plugin-mlvconverter" OFF)
option(ENABLE_GST_PLUGIN_MLVCLASSIFICATION "Enable gst-plugin-mlvclassification" OFF)
option(ENABLE_GST_PLUGIN_MLVSUPERRESOLUTION "Enable gst-plugin-mlvsuperresolution" OFF)
option(ENABLE_GST_PLUGIN_MLVDETECTION "Enable gst-plugin-mlvdetection" OFF)
option(ENABLE_GST_PLUGIN_MLVPOSE "Enable gst-plugin-mlvpose" OFF)
option(ENABLE_GST_PLUGIN_MLVSEGMENTATION "Enable gst-plugin-mlvsegmentation" OFF)
option(ENABLE_GST_PLUGIN_MLTOOLS "Enable gst-plugin-mltools" OFF)
option(ENABLE_GST_PLUGIN_MLTFLITE "Enable gst-plugin-mltflite" OFF)
option(ENABLE_GST_PLUGIN_MLSNPE "Enable gst-plugin-mlsnpe" OFF)
option(ENABLE_GST_PLUGIN_MLQNN "Enable gst-plugin-mlqnn" OFF)
option(ENABLE_GST_PLUGIN_MLMETAPARSER "Enable gst-plugin-mlmetaparser" OFF)
option(ENABLE_GST_PLUGIN_METATRANSFORM "Enable gst-plugin-metatransform" OFF)
option(ENABLE_GST_PLUGIN_OBJTRACKER "Enable gst-plugin-objtracker" OFF)
option(ENABLE_GST_PLUGIN_MLMETAEXTRACTOR "Enable gst-plugin-mlmetaextractor" OFF)
option(ENABLE_GST_PLUGIN_MLPOSTPROCESS "Enable gst-plugin-mlpostprocess" OFF)
option(ENABLE_GST_PLUGIN_MLBIN "Enable gst-plugin-mlbin" OFF)
option(ENABLE_GST_PLUGIN_HEXAGON "Enable gst-plugin-hexagon" OFF)

# === Sample Apps and Test Framework ===
option(ENABLE_GST_SAMPLE_APPS "Enable gst-sample-apps" OFF)
option(ENABLE_GST_TEST_FRAMEWORK "Enable gst-test-framework" OFF)

# === Messaging Plugins ===
option(ENABLE_GST_PLUGIN_MSGBROKER "Enable gst-plugin-msgbroker" OFF)

# === ACK SP Plugins ===
option(ENABLE_GST_PLUGIN_CODEC2 "Enable gst-plugin-codec2" OFF)
option(ENABLE_GST_PLUGIN_CV_IMGPIRAMID "Enable gst-plugin-cv-imgpyramid" OFF)
option(ENABLE_GST_PLUGIN_CV_OPTCLFLOW "Enable gst-plugin-cv-optclflow" OFF)
option(ENABLE_GST_PLUGIN_DFS "Enable gst-plugin-dfs" OFF)
option(ENABLE_GST_PLUGIN_DNGPACKER "Enable gst-plugin-dngpacker" OFF)
option(ENABLE_GST_PLUGIN_DRMDECRYPTOR "Enable gst-plugin-drmdecryptor" OFF)
option(ENABLE_GST_PLUGIN_MLAIC "Enable gst-plugin-mlaic" OFF)
option(ENABLE_GST_PLUGIN_EXAMPLES "Enable gst-plugin-examples" OFF)
option(ENABLE_GST_PYTHON_EXAMPLES "Enable gst-python-examples" OFF)
option(ENABLE_GST_UMD_DAEMON "Enable gst-umd-daemon" OFF)

# === Core Plugins ===
add_subdirectory(gst-plugin-base)
add_subdirectory(gst-plugin-tools)

if(ENABLE_GST_PLUGIN_QMMFSRC)
  add_subdirectory(gst-plugin-qmmfsrc)
endif()

if(ENABLE_GST_PLUGIN_VCOMPOSER)
  add_subdirectory(gst-plugin-vcomposer)
endif()

if(ENABLE_GST_PLUGIN_BATCH)
  add_subdirectory(gst-plugin-batch)
endif()

if(ENABLE_GST_PLUGIN_METAMUX)
  add_subdirectory(gst-plugin-metamux)
endif()

if(ENABLE_GST_PLUGIN_SOCKET)
  add_subdirectory(gst-plugin-socket)
endif()

if(ENABLE_GST_PLUGIN_VSPLIT)
  add_subdirectory(gst-plugin-vsplit)
endif()

if(ENABLE_GST_PLUGIN_VTRANSFORM)
  add_subdirectory(gst-plugin-vtransform)
endif()

if(ENABLE_GST_PLUGIN_OVERLAY)
  add_subdirectory(gst-plugin-overlay)
endif()

if(ENABLE_GST_PLUGIN_VOVERLAY)
  add_subdirectory(gst-plugin-voverlay)
endif()

if(ENABLE_GST_PLUGIN_RESTRICTED_ZONE)
  add_subdirectory(gst-plugin-restricted-zone)
endif()

if(ENABLE_GST_PLUGIN_RTSPBIN)
  add_subdirectory(gst-plugin-rtspbin)
endif()

if(ENABLE_GST_PLUGIN_REDISSINK)
  add_subdirectory(gst-plugin-redissink)
endif()

if(ENABLE_GST_PLUGIN_SMARTVENCBIN)
  add_subdirectory(gst-plugin-smartvencbin)
endif()

if(ENABLE_GST_PLUGIN_CAMIMGREPROC)
  add_subdirectory(gst-plugin-camimgreproc)
endif()

if(ENABLE_GST_PLUGIN_CAMREPROC)
  add_subdirectory(gst-plugin-camreproc)
endif()

if(ENABLE_GST_PLUGIN_JPEGENC)
  add_subdirectory(gst-plugin-jpegenc)
endif()

if(ENABLE_GST_PLUGIN_JPEGPACKER)
  add_subdirectory(gst-plugin-jpegpacker)
endif()

if(ENABLE_GST_PLUGIN_VIDEOTEMPLATE)
  add_subdirectory(gst-plugin-videotemplate)
endif()

if(ENABLE_GST_PLUGIN_HEIFMUX)
  add_subdirectory(gst-plugin-heifmux)
endif()

# === ML Plugins ===
if(ENABLE_GST_PLUGIN_MLACONVERTER)
  add_subdirectory(gst-plugin-mlaconverter)
endif()

if(ENABLE_GST_PLUGIN_MLACLASSIFICATION)
  add_subdirectory(gst-plugin-mlaclassification)
endif()

if(ENABLE_GST_PLUGIN_MLDEMUX)
  add_subdirectory(gst-plugin-mldemux)
endif()

if(ENABLE_GST_PLUGIN_MLVCONVERTER)
  add_subdirectory(gst-plugin-mlvconverter)
endif()

if(ENABLE_GST_PLUGIN_MLVCLASSIFICATION)
  add_subdirectory(gst-plugin-mlvclassification)
endif()

if(ENABLE_GST_PLUGIN_MLVSUPERRESOLUTION)
  add_subdirectory(gst-plugin-mlvsuperresolution)
endif()

if(ENABLE_GST_PLUGIN_MLVDETECTION)
  add_subdirectory(gst-plugin-mlvdetection)
endif()

if(ENABLE_GST_PLUGIN_MLVPOSE)
  add_subdirectory(gst-plugin-mlvpose)
endif()

if(ENABLE_GST_PLUGIN_MLVSEGMENTATION)
  add_subdirectory(gst-plugin-mlvsegmentation)
endif()

if(ENABLE_GST_PLUGIN_MLTOOLS)
  add_subdirectory(gst-plugin-mltools)
endif()

if(ENABLE_GST_PLUGIN_MLTFLITE)
  add_subdirectory(gst-plugin-mltflite)
endif()

if(ENABLE_GST_PLUGIN_MLSNPE)
  add_subdirectory(gst-plugin-mlsnpe)
endif()

if(ENABLE_GST_PLUGIN_MLQNN)
  add_subdirectory(gst-plugin-mlqnn)
endif()

if(ENABLE_GST_PLUGIN_MLMETAPARSER)
  add_subdirectory(gst-plugin-mlmetaparser)
endif()

if(ENABLE_GST_PLUGIN_METATRANSFORM)
  add_subdirectory(gst-plugin-metatransform)
endif()

if(ENABLE_GST_PLUGIN_OBJTRACKER)
  add_subdirectory(gst-plugin-objtracker)
endif()

if(ENABLE_GST_PLUGIN_MLMETAEXTRACTOR)
  add_subdirectory(gst-plugin-mlmetaextractor)
endif()

if(ENABLE_GST_PLUGIN_MLPOSTPROCESS)
  add_subdirectory(gst-plugin-mlpostprocess)
endif()

if(ENABLE_GST_PLUGIN_MLBIN)
  add_subdirectory(gst-plugin-mlbin)
endif()

if(ENABLE_GST_PLUGIN_HEXAGON)
  add_subdirectory(gst-plugin-hexagon)
endif()

# === Sample Apps and Test Framework ===
add_subdirectory(gst-sample-apps/gst-sample-apps-utils)

if(ENABLE_GST_SAMPLE_APPS)
  add_subdirectory(gst-sample-apps)
endif()

if(ENABLE_GST_TEST_FRAMEWORK)
  add_subdirectory(gst-test-framework)
endif()

# === Messaging Plugins ===
if(ENABLE_GST_PLUGIN_MSGBROKER)
  add_subdirectory(gst-plugin-msgbroker)
endif()

# === ACK SP Plugins ===
if(ENABLE_GST_PLUGIN_CODEC2)
  add_subdirectory(gst-plugin-codec2)
endif()

if(ENABLE_GST_PLUGIN_CV_IMGPIRAMID)
  add_subdirectory(gst-plugin-cv-imgpyramid)
endif()

if(ENABLE_GST_PLUGIN_CV_OPTCLFLOW)
  add_subdirectory(gst-plugin-cv-optclflow)
endif()

if(ENABLE_GST_PLUGIN_DFS)
  add_subdirectory(gst-plugin-dfs)
endif()

if(ENABLE_GST_PLUGIN_DNGPACKER)
  add_subdirectory(gst-plugin-dngpacker)
endif()

if(ENABLE_GST_PLUGIN_DRMDECRYPTOR)
  add_subdirectory(gst-plugin-drmdecryptor)
endif()

if(ENABLE_GST_PLUGIN_MLAIC)
  add_subdirectory(gst-plugin-mlaic)
endif()

if(ENABLE_GST_PLUGIN_EXAMPLES)
  add_subdirectory(gst-plugin-examples)
endif()

if(ENABLE_GST_PYTHON_EXAMPLES)
  add_subdirectory(gst-python-examples)
endif()

if(ENABLE_GST_UMD_DAEMON)
  add_subdirectory(gst-umd-daemon)
endif()
