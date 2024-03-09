#Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
#SPDX-License-Identifier: BSD-3-Clause-Clear

#Tar and compress the TFLite SDK into a file in this directory called tflite_sdk.tgz
#Tar and compress the QNN SDK into a file in this directory called qnn_sdk.tgz
#       cd this dir
#       download aistack from qpm
#       copy TFLite SDK
#       ./mltools.sh

FROM k2cmybaseledeps:latest

#See above for instructions regarding creating tflite_sdk.tgz and
#qnn_sdk.tgz before building the Docker
ADD ./tflite_sdk.tgz /
ADD ./qnn_sdk.tgz /

#docker build -t qcombundle --platform=linux/arm64 .
