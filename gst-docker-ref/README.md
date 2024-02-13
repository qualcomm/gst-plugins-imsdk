h/w Acceleration in docker container
=====================================

    runtimedepsimage
           |
           |
           |
       mltoolsimage
           |
           |
           |
      gstcoreimage
           |
           |
           |
    qimpluginsimage
           |
           |
           |
      finalappimage


runtimedepsimage (FROM ubuntu:latest):
    -This container has all run time dependencies to run a h/w component bound application like Gstreamer pipline.
    -Run time dependencies can be fetched using availble tools like strace, ldd, ojbdump etc.
    -Once depenedencies are identified, existing scripts can be applied to fetch those from eSDK.

mltoolsimage (FROM runtimedepsimage):
    -This container has qnn and tflite delegates to make it ready for ML applications.
    -qnn and tflite sdk can be downloaded from Qualcomm Package Manager.
    -to prepare this image: mltoolsimage/README.md

gstcoreimage (FROM mltoolsimage):
    -This container has open source gstreamer plugins.
    -to prepare this image: gstcoreimage/README.md

qimpluginsimage (FROM gstcoreimage):
    -This container has qti plugins which may depends on gstcore.
    -to prepare this image: qimpluginsimage/README.md

finalappimage (FROM qimpluginsimage):
    -This container has application data. IT may have offline video, tflitemodel, tflitelable and other app final environemnt setup.
