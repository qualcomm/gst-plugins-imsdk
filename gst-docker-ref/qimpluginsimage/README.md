Gstreamer Qualcomm plugins image
====================================
Contains all Qualcomm Gstreamer plugins. To create this image, all open source plugins should be removed
from downloaded qim sdk packages.
    -get the qim sdk
    -unzip the package_rel.zip
    -remove all opensource gst packages (kept only qti)
    -cp extrct_ipk.sh and extract ipks
    -tar -czvf  -C qimplugins.tgz ./packages_rel/ ./usr/

qimplugins.tgz is created and can be added to gstcoreimage.
