LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE := ImmEmulatorJ
LOCAL_SRC_FILES := immdevice.cpp immeffect.cpp immvibe.cpp immvibeemu.cpp immos.cpp immdriver.cpp immvibemm.cpp
LOCAL_CFLAGS += -Ofast -mfloat-abi=softfp -DNDEBUG -std=c++17
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)