Gstreamer Open Source plugin image
====================================
Contains all open source gstreamer plugins. To create this image, qti plugins should be removed first
from downloaded qim sdk.

        -get the qim sdk
        -unzip the package_rel.zip
        -remove all qti packages
        -extract ipks using extrct_ipk.sh
        -tar -czvf gstcore.tgz -C ./packages_rel/ ./usr/

gstcore.tgz is created and can be added to gstcoreimage.
