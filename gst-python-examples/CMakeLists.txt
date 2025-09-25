cmake_minimum_required(VERSION 3.16)
project(GST_QTI_PYTHON_EXAMPLES)

set(PYTHON_SCRIPTS
  gst-ai-object-detection.py
  gst-camera-encode.py
  gst-camera-opencv-resize.py
  gst-camera-rotate-downscale-file.py
  gst-camera-three-stream-encode-file-detection-display-classification-rtsp.py
  gst-camera-two-stream-detection-and-classification-side-by-side.py
  gst-camera-two-stream-encode-file-detection-display.py
  gst-concurrent-videoplay-composition.py
  gst-daisychain-detection-pose.py
  gst-filesrc-2detection-classification-segmentation-side-by-side.py
  gst-gui-launcher-app.py
  gst-jpg-image-decode.py
  gst-multi-camera-stream-example.py
  gst-opencv-transform.py
  gst-parallel-inference.py
  gst-rtspsrc-detection-display.py
  gst_utils.py
  gst-video-source.py
  gst-ai-audio-inference.py
  gst-ai-audio-postprocess.py
  gst-ai-audio-preprocess.py
  gst-ai-video-classification.py
  gst-ai-video-classification-and-detection.py
  gst-ai-video-daisychain-pose-estimation.py
  gst-ai-video-detection.py
  gst-ai-video-inference.py
  gst-ai-video-postprocess.py
  gst-ai-video-preprocess.py
  gst-ai-video-preprocess-crop.py
  gst-ai-video-segmentation.py
)

set(QDEMO_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/files")

install(
  PROGRAMS ${PYTHON_SCRIPTS}
  DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

install(
  PROGRAMS ${QDEMO_ASSETS_DIR}/Qdemo
  DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

install(
  FILES
    ${QDEMO_ASSETS_DIR}/Qdemo.png
    ${QDEMO_ASSETS_DIR}/Qdemo.gif
  DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_MEDIA}
  PERMISSIONS OWNER_READ OWNER_WRITE
              GROUP_READ
              WORLD_READ
)
