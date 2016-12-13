# Init scripts
PRODUCT_PACKAGES += \
    fstab.qcom \
    init.qcom.fm.sh \
    init.target.rc

# Temp, until we find a better way
PRODUCT_COPY_FILES += \
    device/xiaomi/ferrari/rootdir/etc/ueventd.qcom.rc:root/ueventd.qcom.rc
