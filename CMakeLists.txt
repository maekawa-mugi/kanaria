cmake_minimum_required(VERSION 3.20)
project(openwnn_std_cpp23 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(openwnn_std STATIC
    src/openwnn_std.cpp
    src/wnnEngine/WnnJpnDic.c
    src/wnnDictionary/engine/ndapi.c
    src/wnnDictionary/engine/ndbdic.c
    src/wnnDictionary/engine/ndcommon.c
    src/wnnDictionary/engine/ndfdic.c
    src/wnnDictionary/engine/ndldic.c
    src/wnnDictionary/engine/ndrdic.c
    src/wnnDictionary/engine/neapi.c
    src/wnnDictionary/engine/necode.c
    src/wnnDictionary/engine/nj_str.c
)

target_include_directories(openwnn_std
    PUBLIC include
    PRIVATE src/wnnDictionary/include
)

if (MSVC)
    target_compile_options(openwnn_std PRIVATE /utf-8)
endif()

add_executable(openwnn_smoke tests/smoke.cpp)
target_link_libraries(openwnn_smoke PRIVATE openwnn_std)

if (MSVC)
    target_compile_options(openwnn_smoke PRIVATE /utf-8)
    target_link_options(openwnn_smoke PRIVATE /ENTRY:wmainCRTStartup)
elseif (WIN32)
    target_link_options(openwnn_smoke PRIVATE -municode)
endif()
