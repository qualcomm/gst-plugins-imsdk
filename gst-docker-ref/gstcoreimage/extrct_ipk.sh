#!/bin/bash

#Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
#SPDX-License-Identifier: BSD-3-Clause-Clear

filelist='ls ./*.ipk'

for ipkfile in $filelist
do
    echo "Going to extract file : "$ipkfile
    ar -xv $ipkfile
    tar -xvf data.tar.xz
done

rm -rf *.ipk
