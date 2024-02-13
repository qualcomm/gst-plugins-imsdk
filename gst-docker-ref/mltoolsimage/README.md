#setup Qcom ml tools package
download qnn package as qistack from Qualcomm Package Manager and get tflite-delegate and tflite-sdk.
QNN sdk is expected to download at /opt/qcom/aistack/qnn/<qnn_version>
populate mltools.sh with:
        WORK_DIR=<WORK_DIR>
        QNN_SDK_DIR=<Where QNN sdk to be downloaded>
        TFLITE_SDK_DIR=<Where TFLite sdk package_rel.zip is downloaded>
        TFLITE_DLGT_DIR=<Where TFLite Delegate is downloaded>

tflite_sdk.tgz and qnn_sdk.tgz are created and can be added to mltoolsimage container.
