#!/bin/bash
set -e

echo "Checking disk space"
df -h .

echo "Downloading SDK"
wget --header="Authorization:${S3_WEBAPP_TOKEN}" -O sdk.sh "${SDK_OBJECT_URL}"

echo "Installing SDK"
chmod +x sdk.sh
./sdk.sh -d ./sdk_install -y

echo "Sourcing toolchain"
source ./sdk_install/environment-setup-armv8a-qcom-linux
echo "Running cmake"
cmake -B build -S . \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DENABLE_GST_IMSDK_PLUGINS=1 \
  -DENABLE_GST_PLUGIN_MLTFLITE=1 \
  -DENABLE_GST_PYTHON_EXAMPLES=1 \
  -DENABLE_GST_SAMPLE_APPS=1 \
  -DENABLE_GST_SAMPLE_APPS_CAMERA=1 \
  -DENABLE_GST_PLUGIN_TOOLS=1 \
  -DENABLE_GST_CAMERA_PLUGINS=1

cmake --build build

echo "Creating install artifacts"
mkdir -p artifacts
DESTDIR="$(realpath artifacts)" cmake --install build --prefix /usr

echo "Creating archive"
tar -czf artifacts.tar.gz artifacts
