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
)

install(
  PROGRAMS ${PYTHON_SCRIPTS}
  DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

