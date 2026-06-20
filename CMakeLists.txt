cmake_minimum_required(VERSION 3.20)
project(kanaria LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(PYTHON3_EXECUTABLE NAMES python3 python py REQUIRED)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_KANARIA_DICTIONARY "${GENERATED_DIR}/KanariaDictionary.c")
set(GENERATED_READABLE_DICTIONARY "${GENERATED_DIR}/readable_dictionary.tsv")
set(RULE_CONNECTION_BLOB_TSV "${CMAKE_CURRENT_SOURCE_DIR}/data/rule_connection_blob.tsv")

file(GLOB READABLE_DICTIONARY_TSV CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/dict/*.tsv"
)

add_custom_command(
    OUTPUT "${GENERATED_READABLE_DICTIONARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gather_readable_dictionary.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/dict"
            "${GENERATED_READABLE_DICTIONARY}"
    DEPENDS
            ${READABLE_DICTIONARY_TSV}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gather_readable_dictionary.py"
    COMMENT "Gathering human-readable dictionary TSV files"
    VERBATIM
)

add_custom_target(kanaria_readable_dictionary ALL
    DEPENDS "${GENERATED_READABLE_DICTIONARY}"
)

add_custom_command(
    OUTPUT "${GENERATED_KANARIA_DICTIONARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${PYTHON3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_kanaria_dictionary.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/dict"
            "${RULE_CONNECTION_BLOB_TSV}"
            "${GENERATED_KANARIA_DICTIONARY}"
    DEPENDS
            ${READABLE_DICTIONARY_TSV}
            "${RULE_CONNECTION_BLOB_TSV}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_kanaria_dictionary.py"
    COMMENT "Generating KanariaDictionary.c from dictionary TSV files"
    VERBATIM
)

add_library(kanaria STATIC
    src/kanaria.cpp
    "${GENERATED_KANARIA_DICTIONARY}"
    src/ndapi.c
    src/ndcommon.c
    src/ndldic.c
    src/ndrdic.c
    src/neapi.c
    src/necode.c
    src/nj_str.c
)

target_include_directories(kanaria
    PUBLIC include
    PRIVATE src
)

if (MSVC)
    target_compile_options(kanaria PRIVATE /utf-8)
endif()

add_executable(kanaria_smoke tests/smoke.cpp)
target_link_libraries(kanaria_smoke PRIVATE kanaria)

include(CTest)
if (BUILD_TESTING)
    add_executable(kanaria_regression tests/regression.cpp)
    target_link_libraries(kanaria_regression PRIVATE kanaria)
    add_test(NAME kanaria_regression COMMAND kanaria_regression)
endif()

add_executable(kanaria_dump_dictionary tools/dump_dictionary.cpp)
target_link_libraries(kanaria_dump_dictionary PRIVATE kanaria)
target_include_directories(kanaria_dump_dictionary PRIVATE src)

if (MSVC)
    target_compile_options(kanaria_smoke PRIVATE /utf-8)
    target_compile_options(kanaria_dump_dictionary PRIVATE /utf-8)
    if (BUILD_TESTING)
        target_compile_options(kanaria_regression PRIVATE /utf-8)
    endif()
    target_link_options(kanaria_smoke PRIVATE /ENTRY:wmainCRTStartup)
elseif (WIN32)
    target_link_options(kanaria_smoke PRIVATE -municode)
endif()
