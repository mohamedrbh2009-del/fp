LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := FallSoft
LOCAL_SRC_FILES := main.cpp
LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_CPPFLAGS  += -std=c++17 -fexceptions -frtti -O2
LOCAL_LDLIBS    := -llog
include $(BUILD_SHARED_LIBRARY)
