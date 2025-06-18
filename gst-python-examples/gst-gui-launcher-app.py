#!/usr/bin/env python3

################################################################################
# Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

import os
import gi
import threading
import subprocess
import urllib.request
import signal

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gio, Gdk, GdkPixbuf, GLib

class DemoWindow(Gtk.Window):
    def __init__(self):
        """Create Demo window"""
        super().__init__()

        # Initialize attributes
        self.check_download = None
        self.download_artifacts = False
        self.ssid = None

        # Check Wi-Fi status every 2 seconds
        self.label = Gtk.Label(label="Wi-Fi status")
        GLib.timeout_add(2000, self.check_wifi_status)

        # Create labels for displaying status, SSID, and IP address
        self.status = Gtk.Label(label="Checking.....")
        self.ssid_label = Gtk.Label(label="SSID:")
        self.ssid_value = Gtk.Label(label="Fetching...")
        self.ip_label = Gtk.Label(label="IP Address:")
        self.ip_value = Gtk.Label(label="Fetching...")

        # Create a Grid with specified column and row spacing
        grid = Gtk.Grid(column_spacing=10, row_spacing=10)
        grid.set_margin_end(20)
        grid.set_margin_start(20)

        # Create a HeaderBar and set its title and subtitle
        header = Gtk.HeaderBar()
        header.set_title("Experience Application Demo")
        header.set_subtitle("Quick reference Demo")
        self.set_titlebar(header)

        # Load the CSS data
        self.css_provider = Gtk.CssProvider()
        self.css_provider.load_from_data(b"""
            headerbar {
                background-color: #2196F3;
                color: blue;
            }
        """)

        # Apply the CSS to the default screen
        screen = Gdk.Screen.get_default()
        style_context = Gtk.StyleContext()
        style_context.add_provider_for_screen(screen, self.css_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        # Create a button with the label "Exit"
        self.exit_app = Gtk.Button(label="Exit")

        # Connect the button to the callback function
        self.exit_app.connect("clicked", self.on_exit_button_clicked)

        # Create a menu bar
        menubar = Gtk.MenuBar()

        # Wi-Fi menu
        wifimenu = Gtk.Menu()
        wifi_item = Gtk.MenuItem(label="Wi-Fi")
        wifi_item.set_submenu(wifimenu)
        menubar.append(wifi_item)

        helpmenu = Gtk.Menu()
        # Add a help item
        help_item = Gtk.MenuItem(label="Help")
        help_item.set_submenu(helpmenu)
        menubar.append(help_item)

        wifi_connect = Gtk.MenuItem(label="Connect")
        wifi_connect.connect("activate", self.establish_connection)
        wifimenu.append(wifi_connect)

        wifi_disconnect = Gtk.MenuItem(label="Disconnect")
        wifi_disconnect.connect("activate", self.disconnect_from_wifi)
        wifimenu.append(wifi_disconnect)

        help_content = Gtk.MenuItem(label="About")
        help_content.connect("activate", self.show_help)
        helpmenu.append(help_content)

        # Create a box to pack widgets
        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        vbox.pack_start(menubar, False, False, 0)
        vbox.pack_start(grid, True, True, 0)
        self.add(vbox)

        # select the source
        self.src_label = Gtk.Label(label="Source")
        sources = [
            "On-Device-Camera",
            "USB-Camera",
        ]
        self.src_select = Gtk.ComboBoxText()
        for src in sources:
            self.src_select.append_text(src)

        # Set the default active application
        self.src_select.set_active(0)

        # select the application
        self.app_label = Gtk.Label(label="Applications")
        applications = [
            "Record live video",
            "DashCamera",
            "VideoWall",
            "ObjectDetection",
            "Parallel-AI-Fusion",
            "Face Detection",
            "Daisychain Pose",
            "Multistream",
            "VideoTestSrc"
        ]
        self.application_select = Gtk.ComboBoxText()
        for app in applications:
            self.application_select.append_text(app)

        # Set the default active application
        self.application_select.set_active(3)

        # Create a "Start" button
        self.start = Gtk.Button.new_with_label("Start")
        self.start.connect("clicked", self.on_start_button_clicked)

        # Add the label to the grid
        grid.attach(self.label, 0, 0, 1, 1)
        grid.attach(self.status, 1, 0, 1, 1)
        grid.attach(self.ssid_label, 0, 1, 1, 1)
        grid.attach(self.ssid_value, 1, 1, 1, 1)
        grid.attach(self.ip_label, 0, 2, 1, 1)
        grid.attach(self.ip_value, 1, 2, 1, 1)
        grid.attach(self.src_label, 0, 3, 1, 1)
        grid.attach(self.app_label, 0, 4, 1, 1)

        # Add the ComboBoxText to the grid
        grid.attach(self.src_select, 1, 3, 1, 1)
        grid.attach(self.application_select, 1, 4, 1, 1)

        # Attach the "Start" button to the grid
        grid.attach(self.start, 1, 5, 1, 1)

        # Attach the "Exit" button to the grid
        grid.attach(self.exit_app, 1, 6, 1, 1)

        # Add an empty label to create a gap
        grid.attach(Gtk.Label(), 1, 7, 1, 1)

        # Add the grid to the window
        self.add(grid)

    def update_ip_address(self):
        # Run the nmcli command to get the IP address of the wlan0 interface
        result = subprocess.run(['nmcli', '-t', '-f', 'IP4.ADDRESS', 'device', 'show', 'wlan0'], capture_output=True, text=True)

        # Extract the IP address from the command output
        ip_address = result.stdout.strip().split('IP4.ADDRESS[1]:')[-1].split('/')[0]

        # Set the extracted IP address to the ip_value attribute
        self.ip_value.set_text(ip_address)

    def establish_connection(self, widget):
        # Create a dialog window for connecting to Wi-Fi
        dialog = Gtk.Dialog(title="Connect to Wi-Fi", transient_for=self, modal=True, destroy_with_parent=True)
        dialog.add_buttons(Gtk.STOCK_OK, Gtk.ResponseType.OK, Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL)

        # Get the content area of the dialog and set spacing between elements
        box = dialog.get_content_area()
        box.set_spacing(10)

        # Create and add a label and entry for the SSID
        ssid_label = Gtk.Label(label="SSID:")
        ssid_entry = Gtk.Entry()
        box.pack_start(ssid_label, True, True, 0)
        box.pack_start(ssid_entry, True, True, 0)

        # Create and add a label and entry for the password
        password_label = Gtk.Label(label="Password:")
        password_entry = Gtk.Entry()
        password_entry.set_visibility(False)
        box.pack_start(password_label, True, True, 0)
        box.pack_start(password_entry, True, True, 0)

        # Show all elements in the dialog
        dialog.show_all()
        response = dialog.run()

        # If the user clicked OK, get the SSID and password, and connect to Wi-Fi
        if response == Gtk.ResponseType.OK:
            self.ssid = ssid_entry.get_text()
            password = password_entry.get_text()
            self.connect_to_wifi(password)

        dialog.destroy()

    def connect_to_wifi(self, password):
        print(f"Connecting to Wi-Fi SSID: {self.ssid} with password: {password}")
        try:
            # Run the nmcli command to connect to the specified Wi-Fi network with the provided password
            subprocess.run(['nmcli', 'dev', 'wifi', 'connect', self.ssid, 'password', password], capture_output=True, text=True, check=True)
            print(f"Connected to Wi-Fi SSID: {self.ssid}")
            self.status.set_text("Wi-Fi is connected")
            # Set text color to green
            self.status.override_color(Gtk.StateFlags.NORMAL, Gdk.RGBA(0, 1, 0, 1))
            self.ssid_value.set_text(self.ssid)
        except subprocess.CalledProcessError as e:
            print(f"Failed to connect to Wi-Fi SSID: {self.ssid}")
            print(e.output)
            self.status.set_text("Failed to connect to Wi-Fi")
            # Set text color to red
            self.status.override_color(Gtk.StateFlags.NORMAL, Gdk.RGBA(1, 0, 0, 1))

    def disconnect_from_wifi(self, ssid):
        try:
            # Run the nmcli command to disconnect from the specified Wi-Fi network
            subprocess.run(['nmcli', 'connection', 'down', self.ssid_value.get_text()], capture_output=True, text=True, check=True)
            print(f"Disconnected from Wi-Fi SSID: {self.ssid_value.get_text()}")
            self.status.set_text("Wi-Fi is disconnected")
            # Set text color to red
            self.status.override_color(Gtk.StateFlags.NORMAL, Gdk.RGBA(1, 0, 0, 1))
            self.ip_value.set_text("Not connected")
        except subprocess.CalledProcessError as e:
            print(f"Failed to disconnect from Wi-Fi SSID: {self.ssid_value.get_text()}")

    def check_wifi_status(self):
        # Run the nmcli command to get the status of all network devices
        result = subprocess.run(['nmcli', '-t', '-f', 'DEVICE,TYPE,STATE,CONNECTION', 'device'], capture_output=True, text=True)
        wifi_connected = False

        # Parse the command output line by line
        for line in result.stdout.splitlines():
            parts = line.split(':')
            # Check if the line has the expected format and if the device is a connected Wi-Fi
            if len(parts) == 4 and parts[1] == 'wifi' and parts[2] == 'connected':
                wifi_connected = True
                self.ssid = parts[3]
                break

        if wifi_connected:
            self.status.set_text("Online")
            # Set text color to green
            self.status.override_color(Gtk.StateFlags.NORMAL, Gdk.RGBA(0, 1, 0, 1))
            self.ssid_value.set_text(self.ssid)
            self.update_ip_address()
        else:
            self.status.set_text("Offline")
            # Set text color to red
            self.status.override_color(Gtk.StateFlags.NORMAL, Gdk.RGBA(1, 0, 0, 1))
            self.ssid_value.set_text("Not connected")
            self.ip_value.set_text("Not connected")
            self.ssid = None

        return True

    def show_help(self, widget):
        # Create a message dialog for displaying application information
        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.INFO,
            buttons=Gtk.ButtonsType.OK,
            text="Applications Information",
        )
        # Format and set the secondary text of the dialog
        dialog.format_secondary_markup(
            "<b>Record Live Video</b>: Records camera feed and saves video.\n\n"
            "<b>DashCamera</b>: Simultaneous streaming from two camera sensors.\n\n"
            "<b>VideoWall</b>: Multichannel video decoding and composition. Creates four instances of a video. Ensure 'Record Live Video' is executed beforehand.\n\n"
            "<b>ObjectDetection</b>: Object detection on input streams from a camera.\n\n"
            "<b>Parallel-AI-Fusion</b>: Parallel AI inferences (Detection, Classification, Pose estimation and Segmentation) from a camera.\n\n"
            "<b>Face Detection</b>: Face detection on input streams from a camera.\n\n"
            "<b>Daisychain Pose</b>: Daisychain of detection and pose estimation from a camera.\n\n"
            "<b>Multistream</b>: Multistream inference from a camera and 7 file streams.\n\n"
        )
        dialog.run()
        dialog.destroy()

    def download_assets(self):
        """
        Downloads necessary assets if they do not already exist.

        Checks for the existence of specific files and downloads them if they are missing.
        """

        file_path = "/etc/media/download_artifacts.sh"
        DEFAULT_SNPE_OBJECT_DETECTION_MODEL = "/etc/models/yolonas.dlc"
        DEFAULT_TFLITE_CLASSIFICATION_MODEL = "/etc/models/inception_v3_quantized.tflite"
        DEFAULT_TFLITE_POSE_DETECTION_MODEL = "/etc/models/hrnet_pose_quantized.tflite"
        DEFAULT_TFLITE_SEGMENTATION_MODEL = "/etc/models/deeplabv3_plus_mobilenet_quantized.tflite"
        DEFAULT_TFLITE_FACE_DETECTION_MODEL = "/etc/models/face_det_lite_quantized.tflite"
        DEFAULT_OBJECT_DETECTION_LABELS = "/etc/labels/yolonas.labels"
        DEFAULT_CLASSIFICATION_LABELS = "/etc/labels/classification.labels"
        DEFAULT_POSE_DETECTION_LABELS = "/etc/labels/posenet_mobilenet_v1.labels"
        DEFAULT_SEGMENTATION_LABELS = "/etc/labels/deeplabv3_resnet50.labels"
        DEFAULT_FACE_DETECTION_LABELS = "/etc/labels/face_detection.labels"
        DEFAULT_INPUT_VIDEO = "/etc/media/video.mp4"

        version = "GA1.4-rel"
        target = "QCS6490"
        output_path = "/etc/models/"

        # Check if all required files and models exist
        if os.path.exists(file_path) \
                and os.path.exists(DEFAULT_SNPE_OBJECT_DETECTION_MODEL) \
                and os.path.exists(DEFAULT_TFLITE_CLASSIFICATION_MODEL) \
                and os.path.exists(DEFAULT_TFLITE_POSE_DETECTION_MODEL) \
                and os.path.exists(DEFAULT_TFLITE_SEGMENTATION_MODEL) \
                and os.path.exists(DEFAULT_TFLITE_FACE_DETECTION_MODEL) \
                and os.path.exists(DEFAULT_OBJECT_DETECTION_LABELS) \
                and os.path.exists(DEFAULT_CLASSIFICATION_LABELS) \
                and os.path.exists(DEFAULT_POSE_DETECTION_LABELS) \
                and os.path.exists(DEFAULT_SEGMENTATION_LABELS) \
                and os.path.exists(DEFAULT_FACE_DETECTION_LABELS) \
                and os.path.exists(DEFAULT_INPUT_VIDEO):
            print("Artifacts already exist.")
            # Close the popup window
            self.popup.destroy()

            # Set the flag to indicate that artifacts are already downloaded
            self.download_artifacts = True
        else:
            print("Aritifacts does not exist.")
            if self.ssid is None:
                # Close the current popup window
                self.popup.destroy()

                # Create a new popup window for WiFi connection
                self.wifi_popup = Gtk.Window(type=Gtk.WindowType.POPUP)

                # Set the parent window for the new popup
                self.wifi_popup.set_transient_for(self)

                # Set the default size of the new popup window
                self.wifi_popup.set_default_size(200, 150)

                # Position the new popup window at the center of the parent window
                self.wifi_popup.set_position(Gtk.WindowPosition.CENTER_ON_PARENT)

                # Move the new popup window to a specific location
                self.wifi_popup.move(10, 10)

                # Add a label to the new popup window
                label = Gtk.Label(label="Connect to network")
                self.wifi_popup.add(label)

                # Show all widgets in the new popup window
                self.wifi_popup.show_all()

                # Close the new popup window after a specified timeout (3 seconds)
                GLib.timeout_add_seconds(3, self.close_popup)

                # Set the flag to indicate that artifacts are not downloaded
                self.download_artifacts = False
            else:
                try:
                    # Show the popup window
                    self.popup.show_all()

                    # URL of the file to be downloaded
                    url = "https://github.com/quic/sample-apps-for-qualcomm-linux/blob/main/download_artifacts.sh?raw=true"

                    # Download the file from the URL to the specified file path
                    urllib.request.urlretrieve(url, file_path)
                    print("File downloaded successfully!")

                    # Change the file permissions to make it executable
                    os.chmod(file_path, 0o755)

                    # Start a subprocess to execute the downloaded file with specified arguments
                    self.process = subprocess.Popen([file_path + " -v " + version + " -c " + target + " -o " + output_path], preexec_fn=os.setsid, shell=True)

                    # Wait for the subprocess to complete
                    self.process.wait()

                    # Set the flag to indicate that artifacts are downloaded
                    self.download_artifacts = True

                except Exception as e:
                    # If an exception occurs, set the flag to indicate that artifacts are not downloaded
                    self.download_artifacts = False

                    # Call the method to handle the cancel button click
                    self.on_cancel_button_clicked()

                finally:
                    # Close the popup window
                    self.popup.destroy()

    def close_popup(self):
        self.wifi_popup.destroy()
        # Returning False to stop the timeout
        return False

    def check_usb_driver(self):
        for i in range(11):
            device = f'/dev/video{i}'
            try:
                # Run the udevadm command and capture the output
                result = subprocess.run(['udevadm', 'info', '-n', device], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

                # Check if the command was successful
                if result.returncode != 0:
                    print(f"Error running udevadm for {device}: {result.stderr}")
                    continue

                # Check if the ID_USB_DRIVER=uvcvideo field is present in the output
                if 'ID_USB_DRIVER=uvcvideo' in result.stdout:
                    print(f"The device {device} is using the uvcvideo driver.")
                    return device
                else:
                    print(f"The device {device} is not using the uvcvideo driver.")
            except Exception as e:
                print(f"An error occurred while checking {device}: {e}")

        # If no device is found, show a GTK pop-up window
        class MessageWindow(Gtk.Window):
            def __init__(self):
                Gtk.Window.__init__(self, title="USB Camera Not Found")
                self.set_border_width(10)
                self.set_default_size(300, 100)

                label = Gtk.Label(label="No USB camera found. Please attach a USB camera.")

                vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
                vbox.pack_start(label, True, True, 0)

                self.add(vbox)

        win = MessageWindow()
        win.connect("destroy", Gtk.main_quit)
        win.show_all()
        Gtk.main()

        return None

    def create_pipeline(self, application, src):
        if self.check_download is not None:
            self.check_download.join()
        pipeline = None

        if src == "USB-Camera":
            found_device = self.check_usb_driver()
            if found_device == None:
                return None
            in_src = f"v4l2src io-mode=dmabuf-import device={found_device} ! video/x-raw,width=640,height=480,framerate=30/1 ! queue ! qtivtransform ! video/x-raw,format=NV12_Q08C"
        elif src == "On-Device-Camera":
            in_src = "qtiqmmfsrc name=camsrc ! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1"

        if application == "Record live video":
           pipeline = "gst-launch-1.0 " + in_src + " ! queue ! tee name=split ! queue ! qtivcomposer ! queue ! \
                gtksink split. ! queue ! v4l2h264enc capture-io-mode=4 output-io-mode=5 ! \
                h264parse ! mp4mux reserved-moov-update-period=1000000 reserved-bytes-per-sec=10000 reserved-max-duration=1000000000 ! filesink location=/etc/media/video_out.mp4"

        elif application == "DashCamera" and src == "On-Device-Camera":
            pipeline = "gst-launch-1.0 -e qtivcomposer name=mix sink_0::position='<0, 0>' sink_0::dimensions='<640, 360>' \
                sink_1::position='<640, 0>' sink_1::dimensions='<640, 360>' \
                mix. ! queue ! gtksink \
                qtiqmmfsrc name=camsrc camera=0 ! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! queue ! mix. \
                qtiqmmfsrc name=camsrc1 camera=1 ! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! queue ! mix. "

        elif application == "DashCamera" and src == "USB-Camera":
            pipeline = f"gst-launch-1.0 -e qtivcomposer name=mix sink_0::position='<0, 0>' sink_0::dimensions='<640, 360>' \
                sink_1::position='<640, 0>' sink_1::dimensions='<640, 360>' \
                mix. ! queue ! gtksink " + in_src + "! queue ! tee name=split ! queue ! mix. \
                split. ! queue ! mix. "

        elif application == "VideoWall":
            if not os.path.exists("/etc/media/video_out.mp4"):
                # Create a new popup window
                popup = Gtk.Window(type=Gtk.WindowType.POPUP)

                # Set the parent window for the new popup
                popup.set_transient_for(self)

                # Set the default size of the new popup window
                popup.set_default_size(400, 50)

                # Position the new popup window at the center of the parent window
                popup.set_position(Gtk.WindowPosition.CENTER_ON_PARENT)

                # Move the new popup window to a specific location
                popup.move(20, 250)

                # Add a label to the new popup window
                label = Gtk.Label(label="Input file is missing. Execute 'Record live video' to save file")
                popup.add(label)

                # Show all widgets in the new popup window
                popup.show_all()

                # Closes after 3 seconds
                GLib.timeout_add_seconds(2, popup.destroy)
            else:
                pipeline = "gst-launch-1.0 -e qtivcomposer name=mix \
                    sink_0::position='<0, 0>' sink_0::dimensions='<640, 360>' \
                    sink_1::position='<640, 0>' sink_1::dimensions='<640, 360>' \
                    sink_2::position='<0, 360>' sink_2::dimensions='<640, 360>' \
                    sink_3::position='<640, 360>' sink_3::dimensions='<640, 360>' \
                    mix. ! queue ! gtksink \
                    filesrc location=/etc/media/video_out.mp4 ! qtdemux ! queue ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! queue ! mix. \
                    filesrc location=/etc/media/video_out.mp4 ! qtdemux ! queue ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! queue ! mix. \
                    filesrc location=/etc/media/video_out.mp4 ! qtdemux ! queue ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! queue ! mix. \
                    filesrc location=/etc/media/video_out.mp4 ! qtdemux ! queue ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! queue ! mix."

        elif application == "VideoTestSrc":
            pipeline = "gst-launch-1.0 " + "videotestsrc" + " ! " + "video/x-raw,width=640" + ",height=480" + " ! " + "gtksink"

        if self.download_artifacts is True:
            if application == "Parallel-AI-Fusion" :
                pipeline = "gst-launch-1.0 \
                    qtivcomposer name=mixer \
                    sink_0::position='<0, 0>' sink_0::dimensions='<960, 540>' \
                    sink_1::position='<0, 0>' sink_1::dimensions='<960, 540>' \
                    sink_2::position='<960, 0>' sink_2::dimensions='<960, 540>' \
                    sink_3::position='<990, 45>' sink_3::dimensions='<320, 180>' \
                    sink_4::position='<0, 540>' sink_4::dimensions='<960, 540>' \
                    sink_5::position='<0, 540>' sink_5::dimensions='<960, 540>' \
                    sink_6::position='<960, 540>' sink_6::dimensions='<960, 540>' \
                    sink_7::position='<960, 540>' sink_7::dimensions='<960, 540>' sink_7::alpha=0.5 \
                    mixer. ! queue ! fpsdisplaysink sync=true signal-fps-measurements=true text-overlay=true video-sink='gtksink' " + in_src + "! queue ! tee name=split \
                    split. ! queue ! mixer. \
                    split. ! queue ! qtimlvconverter ! queue ! qtimlsnpe delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    split. ! queue ! mixer. \
                    split. ! queue ! qtimlvconverter ! queue ! qtimltflite delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options='QNNExternalDelegate,backend_type=htp,htp_device_id=(string)0,htp_performance_mode=(string)2' model=/etc/models/inception_v3_quantized.tflite ! queue ! qtimlvclassification extra-operation=1 threshold=40.0 results=2 module=mobilenet labels=/etc/labels/classification.labels constants='Mobilenet,q-offsets=<38.0>,q-scales=<0.15008972585201263>;' ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    split. ! queue ! mixer. \
                    split. ! queue ! qtimlvconverter ! queue ! qtimltflite delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options='QNNExternalDelegate,backend_type=htp,htp_device_id=(string)0,htp_performance_mode=(string)2' model=/etc/models/hrnet_pose_quantized.tflite ! queue ! qtimlvpose threshold=51.0 results=2 module=hrnet labels=/etc/labels/posenet_mobilenet_v1.labels constants='hrnet,q-offsets=<8.0>,q-scales=<0.0040499246679246426>;' ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    split. ! queue ! mixer. \
                    split. ! queue ! qtimlvconverter ! queue ! qtimltflite delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options='QNNExternalDelegate,backend_type=htp,htp_device_id=(string)0,htp_performance_mode=(string)2' model=/etc/models/deeplabv3_plus_mobilenet_quantized.tflite ! queue ! qtimlvsegmentation module=deeplab-argmax labels=/etc/labels/deeplabv3_resnet50.labels constants='deeplab,q-offsets=<0.0>,q-scales=<1.0>;' ! video/x-raw,format=BGRA,width=256,height=144 ! queue ! mixer. \
                    "
            elif application == "ObjectDetection":
                pipeline = "gst-launch-1.0 " + in_src + "! queue ! tee name=split \
                    split. ! queue ! qtivcomposer name=mixer ! queue ! gtksink \
                    split. ! queue ! qtimlvconverter ! queue ! qtimlsnpe delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' ! queue ! \
                    qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! \
                    video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer."
            elif application == "Face Detection":
                if src == "On-Device-Camera":
                    in_src = "qtiqmmfsrc name=camsrc ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1"
                pipeline = "gst-launch-1.0 -e \
                    qtimlvconverter name=stage_01_preproc mode=image-batch-non-cumulative \
                    qtimltflite name=stage_01_inference model=/etc/models/face_det_lite_quantized.tflite delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options='QNNExternalDelegate,backend_type=htp;' \
                    qtimlvdetection name=stage_01_postproc stabilization=false threshold=51.0 results=6 module=qfd labels=/etc/labels/face_detection.labels constants='qfd,q-offsets=<178.0, 0.0, 102.0>,q-scales=<0.03400895744562149, 0.21995200216770172, 0.1414264440536499>;' \
                    " + in_src + " ! queue ! tee name=split ! queue ! qtivcomposer name=mixer ! queue ! gtksink \
                    split. ! queue ! stage_01_preproc. stage_01_preproc. ! queue ! stage_01_inference. stage_01_inference. ! queue ! stage_01_postproc. stage_01_postproc. ! video/x-raw ! queue ! mixer."
            elif application == "Daisychain Pose":
                pipeline = "gst-launch-1.0 -e \
                    qtimlvconverter name=stage_01_preproc mode=image-batch-non-cumulative \
                    qtimlsnpe name=stage_01_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    qtimlvdetection name=stage_01_postproc threshold=51.0 results=4 module=yolo-nas labels=/etc/labels/yolonas.labels \
                    qtimlvconverter name=stage_02_preproc mode=roi-batch-cumulative image-disposition=centre  \
                    qtimltflite name=stage_02_inference delegate=external external-delegate-path=libQnnTFLiteDelegate.so external-delegate-options='QNNExternalDelegate,backend_type=htp;' model=/etc/models/hrnet_pose_quantized.tflite \
                    qtimlvpose name=stage_02_postproc threshold=51.0 results=1 module=hrnet constants='hrnet,q-offsets=<0.0>,q-scales=<0.0035696658305823803>;' labels=/etc/labels/hrnet_pose.labels \
                    " + in_src + " ! queue ! tee name=t_split_1 \
                    t_split_1. ! queue ! metamux_1. \
                    t_split_1. ! queue ! stage_01_preproc. stage_01_preproc. ! queue ! stage_01_inference. stage_01_inference. ! queue ! stage_01_postproc. stage_01_postproc. ! text/x-raw ! queue ! metamux_1. \
                    qtimetamux name=metamux_1 ! queue ! tee name=t_split_2 \
                    t_split_2. ! queue ! metamux_2. \
                    t_split_2. ! queue ! stage_02_preproc. stage_02_preproc. ! queue ! stage_02_inference. stage_02_inference. ! queue ! stage_02_postproc. stage_02_postproc. ! text/x-raw ! queue ! metamux_2. \
                    qtimetamux name=metamux_2 ! queue ! tee name=t_split_3 \
                    t_split_3. ! queue ! mixer. \
                    t_split_3. ! queue ! qtivsplit name=vsplit src_0::mode=single-roi-meta src_1::mode=single-roi-meta \
                    vsplit. ! video/x-raw,width=240,height=480,format=RGBA ! queue ! mixer. \
                    vsplit. ! video/x-raw,width=240,height=480,format=RGBA ! queue ! mixer. \
                    qtivcomposer name=mixer \
                    sink_0::position='<0, 0>' sink_0::dimensions='<1920, 1080>' \
                    sink_1::position='<0, 0>' sink_1::dimensions='<240, 480>' \
                    sink_2::position='<1680, 0>' sink_2::dimensions='<240, 480>' \
                    mixer. ! queue ! qtivoverlay  engine=gles ! gtksink"
            elif application == "Multistream":
                pipeline = "gst-launch-1.0 -e qtivcomposer name=mixer \
                    sink_0::position='<0, 0>' sink_0::dimensions='<480, 540>' \
                    sink_1::position='<480, 0>' sink_1::dimensions='<480, 540>' \
                    sink_2::position='<960, 0>' sink_2::dimensions='<480, 540>' \
                    sink_3::position='<1440, 0>' sink_3::dimensions='<480, 540>' \
                    sink_4::position='<0, 540>' sink_4::dimensions='<480, 540>' \
                    sink_5::position='<480, 540>' sink_5::dimensions='<480, 540>' \
                    sink_6::position='<960, 540>' sink_6::dimensions='<480, 540>' \
                    sink_7::position='<1440, 540>' sink_7::dimensions='<480, 540>' \
                    sink_8::position='<0, 0>' sink_8::dimensions='<480, 540>' \
                    sink_9::position='<480, 0>' sink_9::dimensions='<480, 540>' \
                    sink_10::position='<960, 0>' sink_10::dimensions='<480, 540>' \
                    sink_11::position='<1440, 0>' sink_11::dimensions='<480, 540>' \
                    sink_12::position='<0, 540>' sink_12::dimensions='<480, 540>' \
                    sink_13::position='<480, 540>' sink_13::dimensions='<480, 540>' \
                    sink_14::position='<960, 540>' sink_14::dimensions='<480, 540>' \
                    sink_15::position='<1440, 540>' sink_15::dimensions='<480, 540>' \
                    mixer. ! queue ! gtksink \
                    split_1. ! qtimlvconverter ! qtimlsnpe name=stage_01_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    split_2. ! qtimlvconverter ! qtimlsnpe name=stage_02_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    split_3. ! qtimlvconverter ! qtimlsnpe name=stage_03_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    split_4. ! qtimlvconverter ! qtimlsnpe name=stage_04_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    split_5. ! qtimlvconverter ! qtimlsnpe name=stage_05_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    split_6. ! qtimlvconverter ! qtimlsnpe name=stage_06_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    split_7. ! qtimlvconverter ! qtimlsnpe name=stage_07_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    split_8. ! qtimlvconverter ! qtimlsnpe name=stage_08_inference delegate=dsp model=/etc/models/yolonas.dlc layers='</heads/Mul,/heads/Sigmoid>' \
                    " + in_src + " ! tee name=split_1 ! queue ! mixer. \
                    filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! tee name=split_2 ! queue ! mixer. \
                    filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! tee name=split_3 ! queue ! mixer. \
                    filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! tee name=split_4 ! queue ! mixer. \
                    filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! tee name=split_5 ! queue ! mixer. \
                    filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! tee name=split_6 ! queue ! mixer. \
                    filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! tee name=split_7 ! queue ! mixer. \
                    filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12 ! tee name=split_8 ! queue ! mixer. \
                    stage_01_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    stage_02_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    stage_03_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    stage_04_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    stage_05_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    stage_06_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    stage_07_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. \
                    stage_08_inference. ! queue ! qtimlvdetection threshold=40.0 results=10 module=yolo-nas labels=/etc/labels/yolonas.labels ! video/x-raw,format=BGRA,width=640,height=360 ! queue ! mixer. "

        if pipeline is not None:
            pipeline_thread = threading.Thread(target=self.execute, args=(pipeline,))
            pipeline_thread.start()

    def on_start_button_clicked(self, widget):
        """
        Handles the start action, creating a popup window with an animated GIF,
        downloading necessary assets, and starting the application pipeline.
        """
        # Create the popup window
        self.popup = Gtk.Window(type=Gtk.WindowType.POPUP)

        # Set to the parent window
        self.popup.set_transient_for(self)
        self.popup.set_default_size(450, 300)
        self.popup.move(55, 0)

        # Load the animated GIF
        gif_path = "/etc/media/Qdemo.gif"
        animation = GdkPixbuf.PixbufAnimation.new_from_file(gif_path)
        gif_image = Gtk.Image.new_from_animation(animation)

        # Create a box to hold the GIF and the close button
        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        vbox.set_name("vbox")

        # Create and add the header
        headerlabel = Gtk.Label(label="Downloading models and labels")
        headerlabel.set_name("headerlabel")
        vbox.pack_start(headerlabel, False, False, 0)
        vbox.pack_start(gif_image, True, True, 0)

        # Create the cancel button
        cancel_button = Gtk.Button(label="cancel")
        cancel_button.connect("clicked", self.on_cancel_button_clicked)
        vbox.pack_start(cancel_button, False, False, 0)
        self.popup.add(vbox)

        # Apply CSS styling
        css_provider = Gtk.CssProvider()
        css_provider.load_from_data(b"""
            #vbox {
                border: 1px solid black;
            }
            #headerlabel {
                padding: 10px;
                background-color: #2196F3;
                color: blue;
            }
        """)
        style_context = Gtk.StyleContext()
        screen = Gdk.Screen.get_default()
        style_context.add_provider_for_screen(screen, css_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        # Add the box to the popup window
        self.popup.show_all()
        self.popup.present_with_time(Gdk.CURRENT_TIME)

        # Get the selected application
        application = self.application_select.get_active_text()
        src = self.src_select.get_active_text()

        # Download models and labels
        if((application == "ObjectDetection") or (application == "Parallel-AI-Fusion") \
            or (application == "Face Detection") or (application == "Daisychain Pose") \
            or (application == "Multistream")):
            self.check_download = threading.Thread(target=self.download_assets)
            self.check_download.start()
        else:
            self.popup.destroy()

        # Start the application pipeline in a separate thread
        threading.Thread(target=self.create_pipeline, args=(application,src,)).start()

    def on_cancel_button_clicked(self, widget):
        # Close the popup window
        self.popup.destroy()

        # Set the flag to indicate that artifacts are not downloaded
        self.download_artifacts = False

        # Reset the check_download attribute
        self.check_download = None

        # Terminate the process group associated with the process
        os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)

        # Wait for the process to terminate
        self.process.wait()

    def execute(self, pipeline):
        """
        Executes a given shell command pipeline.

        Args:
            pipeline (str): The shell command pipeline to execute.
        """
        # Run the shell command pipeline
        try:
            subprocess.run(pipeline, shell=True)
        except Exception as e:
            print("Exception while execution")

    def on_exit_button_clicked(self, widget):
        """Exit the application"""
        os.killpg(os.getpgid(os.getpid()), signal.SIGTERM)
        Gtk.main_quit()

if __name__ == "__main__":
    # Create an instance of the DemoWindow class
    demo = DemoWindow()

    # Show all widgets in the demo window
    demo.show_all()

    # Start the GTK main loop
    Gtk.main()
