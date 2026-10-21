function(standaloneview_use_sdk target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE NO_MALLOC_OVERRIDE)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../include/Interface"
        "${METAHOOK_SOURCE_PATH}/include"
        "${METAHOOK_SOURCE_PATH}/include/Interface"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/common"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/engine"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/cl_dll"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/pm_shared"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/public"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier0"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier1"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier3"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/vstdlib")
endfunction()
