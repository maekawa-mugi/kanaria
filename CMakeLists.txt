cmake_minimum_required(VERSION 3.20)
project(kanaria LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

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

add_library(kanaria_frontend STATIC
    src/frontend/kanaria_input_state.cpp
)
target_link_libraries(kanaria_frontend PUBLIC kanaria)
target_include_directories(kanaria_frontend PUBLIC src/frontend)

if (NOT WIN32)
    set_target_properties(kanaria kanaria_frontend PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

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

add_executable(kanaria_benchmark tests/benchmark.cpp)
target_link_libraries(kanaria_benchmark PRIVATE kanaria)

set(KANARIA_ICON_SIZES 16 22 24 32 48 64 128 256 512)
foreach(KANARIA_ICON_SIZE IN LISTS KANARIA_ICON_SIZES)
    install(FILES
        "artwork/icons/hicolor/${KANARIA_ICON_SIZE}x${KANARIA_ICON_SIZE}/apps/kanaria.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${KANARIA_ICON_SIZE}x${KANARIA_ICON_SIZE}/apps"
    )
endforeach()
install(FILES "artwork/icons/hicolor/scalable/apps/kanaria.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)

if (WIN32)
    add_library(kanaria_tsf SHARED
        src/tsf/kanaria_tsf.cpp
        src/tsf/kanaria_tsf.def
    )
    target_link_libraries(kanaria_tsf
        PRIVATE
            kanaria
            ole32
            oleaut32
            uuid
            advapi32
            user32
            gdi32
    )
    target_compile_definitions(kanaria_tsf PRIVATE UNICODE _UNICODE)
    if (MSVC)
        target_compile_options(kanaria_tsf PRIVATE /utf-8)
    endif()
endif()

option(KANARIA_BUILD_IBUS "Build the ibus-kanaria frontend when ibus-1.0 is available" ON)

if (KANARIA_BUILD_IBUS AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(IBUS QUIET IMPORTED_TARGET ibus-1.0)
    endif()

    if (IBUS_FOUND)
        add_executable(ibus-kanaria src/ibus/kanaria_ibus.cpp)
        target_link_libraries(ibus-kanaria PRIVATE kanaria_frontend PkgConfig::IBUS)

        configure_file(
            src/ibus/kanaria.xml.in
            "${CMAKE_CURRENT_BINARY_DIR}/ibus-kanaria.xml"
            @ONLY
        )

        install(TARGETS ibus-kanaria
            RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}"
        )
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ibus-kanaria.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/ibus/component"
        )
    else()
        message(STATUS "ibus-1.0 was not found; skipping ibus-kanaria")
    endif()
endif()

option(KANARIA_BUILD_FCITX5 "Build the fcitx5-kanaria frontend when Fcitx5Core is available" ON)

if (KANARIA_BUILD_FCITX5 AND NOT WIN32)
    find_package(Fcitx5Core QUIET)

    if (Fcitx5Core_FOUND)
        if (COMMAND add_fcitx5_addon)
            add_fcitx5_addon(fcitx5-kanaria src/fcitx5/kanaria_fcitx5.cpp)
        else()
            add_library(fcitx5-kanaria MODULE src/fcitx5/kanaria_fcitx5.cpp)
        endif()

        target_link_libraries(fcitx5-kanaria PRIVATE kanaria_frontend Fcitx5::Core)

        configure_file(
            src/fcitx5/kanaria-addon.conf.in
            "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-kanaria-addon.conf"
            @ONLY
        )
        configure_file(
            src/fcitx5/kanaria.conf.in
            "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-kanaria.conf"
            @ONLY
        )

        if (NOT DEFINED FCITX_INSTALL_PKGDATADIR)
            set(FCITX_INSTALL_PKGDATADIR "${CMAKE_INSTALL_DATADIR}/fcitx5")
        endif()

        install(TARGETS fcitx5-kanaria
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
        )
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-kanaria-addon.conf"
            RENAME kanaria.conf
            DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
        )
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fcitx5-kanaria.conf"
            RENAME kanaria.conf
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
        )
    else()
        message(STATUS "Fcitx5Core was not found; skipping fcitx5-kanaria")
    endif()
endif()

if (MSVC)
    target_compile_options(kanaria_smoke PRIVATE /utf-8)
    target_compile_options(kanaria_dump_dictionary PRIVATE /utf-8)
    target_compile_options(kanaria_benchmark PRIVATE /utf-8)
    if (BUILD_TESTING)
        target_compile_options(kanaria_regression PRIVATE /utf-8)
    endif()
    target_link_options(kanaria_smoke PRIVATE /ENTRY:wmainCRTStartup)
elseif (WIN32)
    target_link_options(kanaria_smoke PRIVATE -municode)
endif()
