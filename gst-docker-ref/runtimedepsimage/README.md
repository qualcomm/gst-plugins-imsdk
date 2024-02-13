Setup Gstreamer run time dependencies
============================================
This demo contains the dependenceies for gstreamer usecases. Dependencies are to be fetched and popluated into libdir
as per applications usecases.

"libdir=(libgstreamer-1.0.so.0"  "libglib-2.0.so.0"  "libgobject-2.0.so.0"
"libc.so.6"  "libgmodule-2.0.so.0"  "libm.so.6" "libgbm.so" "libgbmutils.so"
"libpcre.so.1"  "libffi.so.8" "libqmmf_recorder_client.so"  "libgstvideo-1.0.so.0"
"libgstbase-1.0.so.0"  "libgstallocators-1.0.so.0"  "libstdc++.so.6"
"libgcc_s.so.1"  "liblog.so.0"  "libbinder.so.0"  "libutils.so.0"  "libcutils.so.0"
"libcamera_metadata.so.0"  "liborc-0.4.so.0"  "libpthread.so.0"
"libgthread-2.0.so.0"  "libdmabufheap.so.0"  "libvmmem.so.0"  "librt.so.1"
"libbase.so.0"  "libion.so.0"  "libadreno_utils.so"  "libgsl.so"  "libz.so.1"
"libsync.so.0" "libgstwayland-1.0.so.0"  "libwayland-client.so.0"
"libgstaudio-1.0.so.0" "libgstrtp-1.0.so.0" "libgstriff-1.0.so.0"
"libgsttag-1.0.so.0"  "libgstpbutils-1.0.so.0"  "libgstcodecparsers-1.0.so.0"
"libc2engine.so"  "libcodec2_vndk.so" "libui.so.0" "libgstqtivideobase.so"
"libIB2C.so"  "libEGL_adreno.so"  "libGLESv2_adreno.so"  "libllvm-glnext.so"
"libeglSubDriverWayland.so" "libwayland-server.so.0" "libllvm-qgl.so"
"libgio-2.0.so.0" "libsndfile.so.1" "libpng16.so.16" "libjpeg.so.62"
"libwayland-cursor.so.0" "libwayland-egl.so.1" "libxml2.so.2" "libcairo.so.2"
"libogg.so.0" "libeva.so" "libpango-1.0.so.0" "libpulse.so.0" "librsvg-2.so.2"
"libvulkan.so.1" "libcurl.so.4" "libjpeg.so.62" "libvorbis.so.0" "libvorbisenc.so.2"
"libcairo-gobject.so.2" "libwebp.so.7" "libavfilter.so.8" "libFLAC.so.8"
"libpixman-1.so.0" "libfreetype.so.6" "libsynx.so" "libcv_common.so"
"libpangocairo-1.0.so.0" "libpixman-1.so.0" "libpulsecommon-15.0.so"
"libdbus-1.so.3" "libOpenCL.so" "libos.so" "libthreadutils.so" "libpangoft2-1.0.so.0"
"libharfbuzz.so.0" "libfontconfig.so.1" "libfribidi.so.0" "libharfbuzz.so.0"
"libwrap.so.0" "libexpat.so.1" "libqcodec2_core.so" "libqcodec2_basecodec.so"
"libqcodec2_platform.so" "libqcodec2_hooks.so" "qti.video.utils.videobufferlayout"
"libqcodec2_base.so" "libqcodec2_utils.so" "libjsoncpp.so.25"
"libqcodec2_v4l2codec.so" "libgdk_pixbuf-2.0.so.0" "libuuid.so.1" "libcdsprpc.so"
"libqtic2module.so" "libOpenCL_adreno.so" "libCB.so" "libllvm-qcom.so"
"libpropertyvault.so.0" "libsbc.so.1" "libasound.so.2" "libcdsprpc.so")

fetch_gstdeps.sh
============================
Populate eSDK_PATH and TARGET_DIR infetch_gstdeps.sh to generates gstdeps.tgz which can be added to mybaseimageledeps container.
