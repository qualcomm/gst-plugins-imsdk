#final container app setup
This container has all final application required artifacts.
example mp4 file:1920_1080_H264_30fps.mp4
tflite model:dv3_argmax_int32.tflite
tflite lables:dv3-argmax.labels
tflite model for benchmarking app:efficientnet_lite0_classification_fp32_2.tflite

docker run:
docker run --rm --mount type=bind,source=/run/user/root,target=/data/wayland --mount type=bind,source=/tmp/docker,target=/tmp/docker \
--mount type=bind,source=/usr/lib/dsp/cdsp/fastrpc_shell_unsigned_3,target=/usr/lib/dsp/cdsp/fastrpc_shell_unsigned_3 \
--mount type=bind,source=/run/udev/data,target=/run/udev/data \
--device=/dev/kgsl-3d0 --device=/dev/dri/card0 --device=/dev/dri/renderD128 --device=/dev/dma_heap/qcom,system \
--device=/dev/video0 --device=/dev/video1 --device=/dev/video2 --device=/dev/video3 --device=/dev/dma_heap/system \
--device=/dev/fastrpc-cdsp-secure -it <IMAGE_ID>

#h/w acceleration example
gst-launch-1.0 -e --gst-debug=2 videotestsrc ! queue! qtivsplit name=vsplit vsplit. ! video/x-raw,width=1280,height=720,format=NV12,compression=ubwc ! queue ! waylandsink x=0 y=0 width=640 height=720 sync=true vsplit. ! qtivtransform rotate=180 ! queue ! video/x-raw,width=1280,height=720,format=NV12,compression=ubwc ! queue ! waylandsink x=1280 y=0 width=640 height=720 sync=true

#multi container example
console#1 (generatig source and using h/w acceleration for video rotaion)
gst-launch-1.0 -e --gst-debug=2 videotestsrc ! qtivtransform rotate=180 ! queue ! video/x-raw\(memory:GBM\),width=1920,height=1080,format=NV12,framerate=30/1 ! queue ! qtisocketsink socket=/tmp/docker/input.sock

console#2 (getting tarnsformed output and displaying with wayland)
gst-launch-1.0 -e --gst-debug=2,qtisocketsrc:6 qtisocketsrc socket=/tmp/docker/input.sock ! video/x-raw\(memory:GBM\),width=1920,height=1080,format=NV12,framerate=30/1 ! queue ! waylandsink fullscreen=true async=true sync=false

#mltool running inside container
gst-launch-1.0 -e --gst-debug=2 qtivcomposer name=mixer sink_1::dimensions="<1920,1080>" sink_1::alpha=0.5 ! queue ! waylandsink sync=false fullscreen=true filesrc location=/data/1920_1080_H264_30fps.mp4 ! qtdemux ! queue ! h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! tee name=split ! queue ! mixer. split. ! queue ! qtimlvconverter ! queue ! qtimltflite delegate=external external-delegate-path=/usr/lib/libQnnTFLiteDelegate.so external-delegate-options="QNNExternalDelegate,backend_type=htp,library_path=/usr/lib/libQnnHtp.so,skel_library_dir=/usr/lib/rfsa/adsp;" model=/data/dv3_argmax_int32.tflite ! queue ! qtimlvsegmentation module=deeplab-argmax labels=/data/dv3-argmax.labels ! video/x-raw,width=256,height=144 ! queue ! mixer.
