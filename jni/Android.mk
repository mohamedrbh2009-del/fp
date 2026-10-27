LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := GTASA_FirstPerson
LOCAL_SRC_FILES := first_person.cpp AML/mod/logger.cpp AML/mod/config.cpp
LOCAL_C_INCLUDES += $(LOCAL_PATH)/AML
LOCAL_CPPFLAGS  += -O2 -std=c++17 -fno-exceptions -fno-rtti
LOCAL_LDLIBS    := -llog -ldl
include $(BUILD_SHARED_LIBRARY)
