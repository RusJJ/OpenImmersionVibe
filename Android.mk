LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE := ImmEmulatorJ
LOCAL_SRC_FILES := immdevice.cpp immeffect.cpp immvibe.cpp immvibeemu.cpp immos.cpp immdriver.cpp immvibemm.cpp immivt.cpp immivtdata.cpp immjni.cpp
LOCAL_CFLAGS += -O2 -Wall -Wextra -DNDEBUG -fvisibility=hidden
LOCAL_CPPFLAGS += -std=c++17 -fno-exceptions -fno-rtti
LOCAL_LDLIBS += -llog
ifeq ($(TARGET_ARCH_ABI),$(filter $(TARGET_ARCH_ABI),arm64-v8a x86_64))
    LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384
endif
include $(BUILD_SHARED_LIBRARY)
