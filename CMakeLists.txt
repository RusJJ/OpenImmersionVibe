cmake_minimum_required(VERSION 3.18.1)
project(ImmEmulatorJ CXX)

add_library(ImmEmulatorJ SHARED
    immdevice.cpp
    immeffect.cpp
    immvibe.cpp
    immvibeemu.cpp
    immos.cpp
    immdriver.cpp
    immvibemm.cpp
    immivt.cpp
    immivtdata.cpp
    immjni.cpp
)

target_include_directories(ImmEmulatorJ PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ImmEmulatorJ PRIVATE cxx_std_17)
target_compile_options(ImmEmulatorJ PRIVATE -O2 -Wall -Wextra -DNDEBUG -fno-exceptions -fno-rtti -fvisibility=hidden)

find_library(log-lib log)
target_link_libraries(ImmEmulatorJ ${log-lib})

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_link_options(ImmEmulatorJ PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)
endif()
