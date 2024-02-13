#Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
#SPDX-License-Identifier: BSD-3-Clause-Clear

#Tar and compress the GStreamer dependencies into a file in this directory called gstdeps.tgz
#       cd to this directory
#       mkdir -p ./gstdeps
#       ./fetch_gstdeps.sh

FROM k2cmybaseimagele:latest AS builder

#stage-2
FROM ubuntu:latest AS final

RUN apt-get clean

RUN ln -snf /usr/share/zoneinfo/$CONTAINER_TIMEZONE /etc/localtime && echo $CONTAINER_TIMEZONE > /etc/timezone

RUN apt-get update && apt-get -y install strace

ENV SDKTARGETSYSROOT=/usr/local/tmp/sysroots/qcm6490

COPY --from=builder $SDKTARGETSYSROOT/lib/libusb-1.0.* /lib/
COPY --from=builder $SDKTARGETSYSROOT/lib/libwrap.* /lib/

#See above for instructions regarding creating gstdeps.tgz before building the Docker image
ADD ./gstdeps.tgz /

#docker build -t mybaseimageledeps --platform=linux/arm64 .
