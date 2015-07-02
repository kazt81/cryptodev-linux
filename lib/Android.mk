LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libcryptodev-lib-benchmark
LOCAL_MODULE_TAGS := eng tests
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MULTILIB := 64
LOCAL_C_INCLUDES := external/openssl/include $(LOCAL_PATH)/../module
LOCAL_CFLAGS := -Wall
LOCAL_SRC_FILES := benchmark.c hash.c threshold.c combo.c
LOCAL_SHARED_LIBRARIES := 
LOCAL_STATIC_LIBRARIES := 

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := cryptodev-lib-benchmark
LOCAL_MODULE_TAGS := eng tests
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MULTILIB := 64
LOCAL_CPP_EXTENSION := .cc
LOCAL_C_INCLUDES := external/openssl/include $(LOCAL_PATH)/../module
LOCAL_CFLAGS := -DDEBUG -Wall
LOCAL_SRC_FILES := main.c
LOCAL_SHARED_LIBRARIES := libssl libcrypto
LOCAL_STATIC_LIBRARIES := libcryptodev-lib-benchmark

include $(BUILD_EXECUTABLE)
