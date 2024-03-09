#!/bin/sh

# Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

SYSROOTDIR="<eSDK_PATH>/tmp/sysroots/qcm6490"
TARGETDIR="<TARGET_DIR>"

cd $SYSROOTDIR;

libdir=("See READ.md")


for val in "${libdir[@]}"
do
	lib=$(find . -name "$val")
	if [ -z "$lib" ]; then
		echo "$val : Not found"
	else
		cp --parents -ap $(echo $lib | sed 's/so.*/*/') $TARGETDIR/gstdeps
	fi
done


cd $TARGETDIR
tar -czvf gstdeps.tgz -C $TARGETDIR/gstdeps/ ./usr/

