LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE        := cryptodev.ko
LOCAL_MODULE_CLASS  := DLKM
LOCAL_MODULE_TAGS   := optional
LOCAL_MODULE_PATH   := $(TARGET_OUT)/lib/modules
CRYPTODEV_BUILD_DIR := $(OUT)/cryptodev
CRYPTODEV_LOCAL_DIR := $(LOCAL_PATH)
CRYPTODEV_VERSION   := 1.7

include $(BUILD_SYSTEM)/base_rules.mk

$(LOCAL_BUILT_MODULE): $(CRYPTODEV_BUILD_DIR)/$(LOCAL_MODULE) | $(ACP)
	$(transform-prebuilt-to-target)

TARGET_KERNEL_ARCH := $(strip $(TARGET_KERNEL_ARCH))
TARGET_KERNEL_CROSS_COMPILE_PREFIX := $(strip $(TARGET_KERNEL_CROSS_COMPILE_PREFIX))
ifeq ($(filter %64,$(TARGET_KERNEL_ARCH)),)
  KERNEL_CROSS_COMPILE := arm-eabi-
  KERNEL_ARCH := arm
  KERNEL_FLAGS :=
else
  KERNEL_CROSS_COMPILE := $(TARGET_KERNEL_CROSS_COMPILE_PREFIX)
  KERNEL_ARCH := $(TARGET_KERNEL_ARCH)
  KERNEL_CFLAGS := KCFLAGS=-mno-android
endif

$(CRYPTODEV_BUILD_DIR)/$(LOCAL_MODULE): $(TARGET_PREBUILT_INT_KERNEL) $(CRYPTODEV_LOCAL_DIR) version.h
	$(hide) mkdir -p $(CRYPTODEV_BUILD_DIR) && \
	cp -f $(CRYPTODEV_LOCAL_DIR)/Makefile $(CRYPTODEV_LOCAL_DIR)/*.c $(CRYPTODEV_LOCAL_DIR)/*.h $(CRYPTODEV_BUILD_DIR) && \
	cp -rf $(CRYPTODEV_LOCAL_DIR)/crypto $(CRYPTODEV_BUILD_DIR) && \
	echo "#define VERSION \"$(CRYPTODEV_VERSION)\"" > $(CRYPTODEV_BUILD_DIR)/version.h && \
	make -s -C kernel O=$(OUT)/obj/KERNEL_OBJ M=$(CRYPTODEV_BUILD_DIR) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) $(KERNEL_CFLAGS) modules
