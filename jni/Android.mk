# Android libretro core: generate Musashi's tables on the host first
#   make -f Makefile.libretro deps/musashi/m68kops.c && ndk-build -C jni
LOCAL_PATH := $(call my-dir)
CORE_DIR   := $(LOCAL_PATH)/..
include $(CORE_DIR)/Makefile.common

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := $(addprefix $(CORE_DIR)/,$(MUSASHI_SRCS) $(LIBRETRO_SRCS))
LOCAL_CFLAGS    := -O3 -fvisibility=hidden $(CORE_DEFINES) $(CORE_INCLUDES)
LOCAL_CPPFLAGS  := -std=c++17
LOCAL_LDFLAGS   := -Wl,--no-undefined
include $(BUILD_SHARED_LIBRARY)
