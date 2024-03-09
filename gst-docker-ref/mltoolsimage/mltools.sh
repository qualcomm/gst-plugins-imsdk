#!/bin/sh

# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

WORK_DIR=<WORK_DIR>
QNN_SDK_DIR=<Where QNN sdk to be downloaded>
TFLITE_SDK_DIR=<Where TFLite sdk package_rel.zip is downloaded>
TFLITE_DLGT_DIR=<Where TFLite Delegate is downloaded>
CDSP_HEXAGON=hexagon-v68

cd ${TFLITE_SDK_DIR}

ar -xv tflite_2.11.1.ipk
tar -xvf data.tar.gz

cd ${WORK_DIR}
cp ${TFLITE_DLGT_DIR}/lib/aarch64-oe-linux-gcc11.2/lib* ${TFLITE_SDK_DIR}/usr/lib/
tar -czvf tflite_sdk.tgz -C ${TFLITE_SDK_DIR} ./usr/

#qnn sdk
mkdir -p ${QNN_SDK_DIR}/usr/lib/rfsa/adsp

cd /opt/qcom/aistack/qnn/<qnn_version>
cp lib/aarch64-oe-linux-gcc11.2/lib* ${QNN_SDK_DIR}/usr/lib/
cp lib/${CDSP_HEXAGON}/unsigned/lib* ${QNN_SDK_DIR}/usr/lib/rfsa/adsp
cp lib/

cd ${WORK_DIR}
tar -czvf qnn_sdk.tgz -C ${QNN_SDK_DIR} ./usr/

