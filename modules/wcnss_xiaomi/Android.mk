LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := wcnss_xiaomi_client.c
LOCAL_C_INCLUDES += hardware/qcom/wlan/wcnss_service
LOCAL_CFLAGS += -Wall

LOCAL_SHARED_LIBRARIES := libc libcutils libutils liblog

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libwcnss_qmi

include $(BUILD_SHARED_LIBRARY)
