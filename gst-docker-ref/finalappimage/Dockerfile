#Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
#SPDX-License-Identifier: BSD-3-Clause-Clear

FROM k2cmyqimimage:latest

#ADD <mp4 file to process> /data/
#ADD <tflite model for ML> /data/
#ADD <tflite lables> /data/
#ADD <tflite model for tflite benchmark app> /data/
ADD ./setup.sh /usr/local/etc/

#to store ml vdetection/vclasification modules
RUN mkdir -p /opt/qti/qimsdk/usr/lib/gstreamer-1.0/ml

ENV XDG_RUNTIME_DIR=/data/wayland
ENV WAYLAND_DISPLAY=wayland-1

WORKDIR /usr/local/etc

#docker build -t myfinalapp --platform=linux/arm64 .
