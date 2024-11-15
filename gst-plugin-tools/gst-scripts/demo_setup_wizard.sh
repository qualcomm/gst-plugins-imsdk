#!/bin/bash

# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

echo "Pipeline builder script initiated!"
pipeline='export XDG_RUNTIME_DIR=/dev/socket/weston && export WAYLAND_DISPLAY=wayland-1 && gst-launch-1.0 -e --gst-debug=2 '
help="In order to run the pipeline:\n"
usbsrc=false

echo "Select pipeline input:"
echo "1) Video file"
echo "2) USB camera"
echo "3) RTSP Source"

while read input
do
    case $input in
        "1")
            echo "Selected input: Video file!"
            pipeline+='filesrc location=/opt/data/input.mp4 ! qtdemux ! h264parse config-interval=1 ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! '
            livesrc=false
            help+="Push the video file on the device in '/opt/data/' and name it 'input.mp4'.\n"
            help+="\n"
            break
            ;;
        "2")
            echo "Selected input: USB camera!"
            read -p 'Please enter the video node for the USB video device (for ex:"/dev/video2"):' device_id
            pipeline+='v4l2src io-mode=dmabuf-import device="'${device_id}'" ! video/x-raw,width=1920,height=1080 ! qtivtransform ! '
            livesrc=true
            usbsrc=true
            help+="Make sure the video device mentioned in v4l2src's device property is the USB camera.\n"
            help+="\n"
            unset device_id
            break
            ;;
        "3")
            echo "Selected input: RTSP source!"
            read -p 'Please specify the RTSP source URI (for ex: "rtsp://admin:qualcomm1@192.168.0.10:554/Streaming/Channels/101"):' rtsp_src
            echo "${rtsp_src}"
            pipeline+="rtspsrc location=${rtsp_src} ! queue ! rtpptdemux ! rtph264depay ! h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! "
            livesrc=true
            help+="Please make sure that the RTSP source and the device are in the same network for RTSP streaming to be possible.\n"
            help+="\n"
            unset rtsp_src
            break
            ;;
        *)
            echo "Invalid input!"
            echo "Select pipeline input:"
            echo "1) Video file"
            echo "2) USB camera"
            echo "3) RTSP source"
            ;;
    esac
done

echo "Select pipeline model:"
echo "1) Detection - FootTrackNet Quantized"
echo "2) Classification - ResNet101 Quantized"
echo "3) Segmentation - FFNet-40S Quantized"

while read input
do
    case $input in
        "1")
            echo "Selected model: Detection - FootTrackNet Quantized!"
            pipeline+='tee name=t_split_0 t_split_0. ! qtimetamux name=metamux t_split_0. ! qtimlvconverter ! queue ! qtimltflite delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options="QNNExternalDelegate,backend_type=htp;" model=/opt/data/foot_track_net_quantized.tflite ! queue ! qtimlvdetection threshold=90.0 results=10 module=qpd constants="qpd,q-offsets=<0.0,25.0,114.0,0.0>,q-scales=<0.0035636962857097387,1.2998489141464233,2.1744511127471924,0.00390625>;" labels=/opt/labels/foot_track_net.labels ! text/x-raw ! queue ! metamux. metamux. ! qtivoverlay engine=gles ! queue ! '
            help+="Please download the model and label files from here: https://aihub.qualcomm.com/models/foot_track_net\n"
            help+="Make sure to select your device and set TorchScript -> TFLite before downloading!\n"
            help+="Push the Model file on the device at '/opt/data/' and name it 'foot_track_net_quantized.tflite'\n"
            help+="Make sure the label file is present at '/opt/labels/foot_track_net.labels'\n"
            help+="\n"
            break
            ;;
        "2")
            echo "Selected model: Classification - ResNet101 Quantized!"
            pipeline+='tee name=t_split_0 t_split_0. ! qtimetamux name=metamux t_split_0. ! qtimlvconverter ! queue ! qtimltflite delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options="QNNExternalDelegate,backend_type=htp;" model=/opt/data/resnet101_quantized.tflite ! queue ! qtimlvclassification threshold=51.0 results=5 module=mobilenet labels=/opt/labels/resnet101.labels extra-operation=softmax constants="Mobilenet,q-offsets=<-82.0>,q-scales=<0.21351955831050873>;" ! text/x-raw ! queue ! metamux. metamux. ! qtioverlay engine=gles ! queue ! '
            help+="Please download the model and label files from here: https://aihub.qualcomm.com/models/resnet101_quantized\n"
            help+="Make sure to select your device and set TorchScript -> TFLite before downloading!\n"
            help+="Push the Model file on the device at '/opt/data/' and name it 'resnet101_quantized.tflite'\n"
            help+="Make sure the label file is present at '/opt/labels/resnet101.labels'\n"
            help+="\n"
            break
            ;;
        "3")
            echo "Selected model: Segmentation - FFNet-40S Quantized!"
            if [ "$usbsrc" = true ]; then
                pipeline+='video/x-raw\,format=RGB ! '
            fi
            pipeline+='tee name=t_split_0 t_split_0. ! queue ! mixer. t_split_0. ! qtimlvconverter ! queue ! qtimltflite delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options="QNNExternalDelegate,backend_type=htp;" model=/opt/data/ffnet_40s_quantized.tflite ! queue ! qtimlvsegmentation module=deeplab-argmax labels=/opt/labels/dv3-argmax.labels constants="FFNet-40S,q-offsets=<50.0>,q-scales=<0.31378185749053955>;" ! queue ! mixer. qtivcomposer name=mixer background=0 sink_0::position="<0, 0>" sink_0::dimensions="<1280, 720>" sink_1::position="<0, 0>" sink_1::dimensions="<1280, 720>" sink_1::alpha=0.5 mixer. ! queue ! '
            help+="Please download the model and label files from here: https://aihub.qualcomm.com/models/ffnet_40s_quantized\n"
            help+="Make sure to select your device and set TorchScript -> TFLite before downloading!\n"
            help+="Push the Model file on the device at '/opt/data/' and name it 'ffnet_40s_quantized.tflite'\n"
            help+="Make sure the label file is present at '/opt/labels/dv3-argmax.labels'\n"
            help+="\n"
            break
            ;;
        *)
            echo "Invalid input!"
            echo "Select pipeline model:"
            echo "1) Detection - FootTrackNet Quantized"
            echo "2) Classification - ResNet101 Quantized"
            echo "3) Segmentation - FFNet-40S Quantized"
            ;;
    esac
done

echo "Select pipeline output:"
echo "1) HDMI - Display"
echo "2) Video file"
echo "3) RTSP out"

while read input
do
    case $input in
        "1")
            echo "Selected output: HDMI - Display!"
            if [ $livesrc = "true" ]; then
                pipeline+='waylandsink sync=false async=false fullscreen=true'
            elif [ $livesrc = "false" ]; then
                pipeline+='waylandsink sync=true async=false fullscreen=true'
            else
                pipeline+='fakesink'
                echo "Unexpected livesrc: ${livesrc}! Using fakesink!"
            fi
            help+="In order for the display to work, you need to have it connected during the device boot!\n"
            help+="If you have booted the device and connected the display afterwards, please reboot the device while the display is connected!\n"
            help+="\n"
            break
            ;;
        "2")
            echo "Selected output: Video file!"
            pipeline+='v4l2h264enc capture-io-mode=5 output-io-mode=5 ! h264parse config-interval=1 ! mp4mux ! filesink location=/opt/data/output.mp4'
            help+="The output from the pipeline will be saved in '/opt/data/' as 'output.mp4'\n"
            help+="\n"
            break
            ;;
        "3")
            echo "Selected output: RTSP out!"
            pipeline+='v4l2h264enc capture-io-mode=5 output-io-mode=5 ! queue ! h264parse config-interval=1 ! queue ! qtirtspbin address=0.0.0.0 port=8900'
            help+="The RTSP out stream is available on port=8900 and mount-point=live (For ex: rtsp://<Device-IP>:8900/live).\n"
            help+="\n"
            break
            ;;
        *)
            echo "Invalid input!"
            echo "Select pipeline output:"
            echo "1) HDMI - Display"
            echo "2) Video file"
            echo "3) RTSP out"
            ;;
    esac
done

echo -e "\n\n\n"
echo Pipeline built:
echo $pipeline

[ -d "/opt/data" ] || mkdir -p /opt/data
echo "#!/bin/bash" > /opt/data/gst_wizard_pipeline.sh
echo $pipeline >> /opt/data/gst_wizard_pipeline.sh
chmod 777 /opt/data/gst_wizard_pipeline.sh

echo -e "\n"
echo "The pipeline built is saved at /opt/data/gst_wizard_pipeline.sh"
help+="The pipeline can be run using \"bash /opt/data/gst_wizard_pipeline.sh\""
help+="\n"
help+="If you want to run AI pipeline with your own video files, AI model and labels, then push them to location '/opt/data' and update GST launch command accordingly to update name of the Model file, label file and sample video file\n"
help+="\n"

echo -e "\n\n"
echo -e $help

unset pipeline help input livesrc usbsrc
