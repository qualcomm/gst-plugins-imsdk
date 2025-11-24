#!/bin/bash

#Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#SPDX-License-Identifier: BSD-3-Clause-Clear

filelist='ls ./*.ipk'

for ipkfile in $filelist
do
    echo "Going to extract file : "$ipkfile
    ar -xv $ipkfile
    tar -xvf data.tar.xz
done

rm -rf *.ipk
