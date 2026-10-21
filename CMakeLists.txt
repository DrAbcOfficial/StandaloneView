cmake_minimum_required(VERSION 3.21)
option(FORMAT_VALIDATION_ONLY "Configure only clang-format targets without native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(FormatValidation LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(StandaloneView LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "StandaloneView requires Windows MSVC x86. Configure with -A Win32.")
endif()
get_property(standaloneview_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(standaloneview_multi_config)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Select Debug or Release.")
endif()
include(cmake/Dependencies.cmake)
standaloneview_prepare_dependencies()
include("${VC_LTL_Root}/VC-LTL helper for cmake.cmake")
include(cmake/SDK.cmake)
include(cmake/SourceGroups.cmake)
add_library(StandaloneView SHARED
    src/plugins.cpp src/privatehook.cpp src/StandaloneViewManager.cpp src/EmptyWorld.cpp
    include/Interface/IStandaloneView.h
    "${METAHOOK_SOURCE_PATH}/include/HLSDK/common/interface.cpp")
standaloneview_assign_source_groups(StandaloneView)
standaloneview_use_sdk(StandaloneView)
target_compile_definitions(StandaloneView PRIVATE WIN32 _WINDOWS _USRDLL _MBCS)
target_compile_options(StandaloneView PRIVATE /MP /Zi /W3 /wd4311 /wd4312 /wd4819 /wd4996)
target_link_options(StandaloneView PRIVATE /DEBUG
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>)
set_target_properties(StandaloneView PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
install(TARGETS StandaloneView RUNTIME DESTINATION svencoop/metahook/plugins)
install(FILES "$<TARGET_PDB_FILE:StandaloneView>" DESTINATION svencoop/metahook/plugins)

option(STANDALONEVIEW_SYNC_GAMEDATA "Download and prune StandaloneView gamedata before building" ON)
set(STANDALONEVIEW_GAMEDATA_DIR "${PROJECT_BINARY_DIR}/assets/svencoop/metahook/gamedata/standaloneview"
    CACHE PATH "StandaloneView gamedata catalog directory")
if(STANDALONEVIEW_SYNC_GAMEDATA)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(StandaloneViewGameData
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync-gamedata.py"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/scripts/manifests/standaloneview.json"
            --target-dir "${STANDALONEVIEW_GAMEDATA_DIR}" --temp-root "${PROJECT_BINARY_DIR}/gamedata-sync"
        COMMENT "Synchronizing StandaloneView gamedata" VERBATIM)
    add_custom_target(StandaloneViewGameDataValidate
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-gamedata.py"
            "${STANDALONEVIEW_GAMEDATA_DIR}" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/scripts/manifests/standaloneview.json"
        COMMENT "Validating StandaloneView gamedata" VERBATIM)
    add_dependencies(StandaloneViewGameDataValidate StandaloneViewGameData)
    add_dependencies(StandaloneView StandaloneViewGameDataValidate)
endif()
install(DIRECTORY "${STANDALONEVIEW_GAMEDATA_DIR}/" DESTINATION svencoop/metahook/gamedata/standaloneview OPTIONAL)

option(STANDALONEVIEW_BUILD_TESTS "Build StandaloneView regression tests" OFF)
if(STANDALONEVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    option(METAHOOKSV_ENABLE_LAUNCH_GAME "Add the Visual Studio build/deploy/debug target" OFF)
    if(METAHOOKSV_ENABLE_LAUNCH_GAME)
        include(cmake/LaunchGame.cmake)
        metahooksv_add_launch_game()
    endif()
endif()

# Shared formatting targets are explicit; ordinary native builds do not run them.
include(cmake/FormatValidation.cmake)
