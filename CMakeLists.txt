cmake_minimum_required(VERSION 3.20)
project(openwnn_std_cpp23 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(PYTHON3_EXECUTABLE NAMES python3 python py REQUIRED)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_WNN_JPN_DIC "${GENERATED_DIR}/WnnJpnDic.c")
set(GENERATED_WNN_JPN_DIC_BLOBS "${GENERATED_DIR}/wnn_jpn_dic_blobs.tsv")
set(GENERATED_READABLE_WNN_JPN_DIC "${GENERATED_DIR}/readable_dictionary.tsv")

file(GLOB READABLE_DICTIONARY_TSV CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/dict/*.tsv"
)

add_custom_command(
    OUTPUT "${GENERATED_READABLE_WNN_JPN_DIC}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gather_readable_dictionary.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/dict"
            "${GENERATED_READABLE_WNN_JPN_DIC}"
    DEPENDS
            ${READABLE_DICTIONARY_TSV}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gather_readable_dictionary.py"
    COMMENT "Gathering human-readable dictionary TSV files"
    VERBATIM
)

add_custom_target(openwnn_readable_dictionary ALL
    DEPENDS "${GENERATED_READABLE_WNN_JPN_DIC}"
)

add_custom_command(
    OUTPUT "${GENERATED_WNN_JPN_DIC_BLOBS}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/extract_wnn_jpn_dic.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/wnnEngine/WnnJpnDic.c"
            "${GENERATED_WNN_JPN_DIC_BLOBS}"
    DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/wnnEngine/WnnJpnDic.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/extract_wnn_jpn_dic.py"
    COMMENT "Extracting WnnJpnDic binary blobs"
    VERBATIM
)

add_custom_command(
    OUTPUT "${GENERATED_WNN_JPN_DIC}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_wnn_jpn_dic.py"
            "${GENERATED_WNN_JPN_DIC_BLOBS}"
            "${GENERATED_WNN_JPN_DIC}"
    DEPENDS
            "${GENERATED_WNN_JPN_DIC_BLOBS}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_wnn_jpn_dic.py"
    COMMENT "Generating WnnJpnDic.c from extracted binary blobs"
    VERBATIM
)

add_library(openwnn_std STATIC
    src/openwnn_std.cpp
    "${GENERATED_WNN_JPN_DIC}"
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

add_executable(openwnn_dump_dictionary tools/dump_dictionary.cpp)
target_link_libraries(openwnn_dump_dictionary PRIVATE openwnn_std)
target_include_directories(openwnn_dump_dictionary PRIVATE src/wnnDictionary/include)

if (MSVC)
    target_compile_options(openwnn_smoke PRIVATE /utf-8)
    target_compile_options(openwnn_dump_dictionary PRIVATE /utf-8)
    target_link_options(openwnn_smoke PRIVATE /ENTRY:wmainCRTStartup)
elseif (WIN32)
    target_link_options(openwnn_smoke PRIVATE -municode)
endif()
