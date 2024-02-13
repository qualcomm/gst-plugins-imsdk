#Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
#SPDX-License-Identifier: BSD-3-Clause-Clear

#Tar and compress the Core GStreamer dependencies into a file in this directory called gstcore.tgz
#   cd to this directory
#   cp qimsdk/package_rel.zip into this dir
#   unzip package_rel.zip and remove all qti packages
#   run ./fetch_gstdeps.sh inside unzipped package_rel
#   tar -czvf gstcore.tgz -C ./packages_rel/ ./usr/

FROM k2cqcombundleimage:latest

#See above for instructions regarding creating gstcore.tgz before building the Docker image
ADD ./gstcore.tgz /

#docker build -t mygstcoreimage --platform=linux/arm64 .
