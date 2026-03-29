cmake_minimum_required(VERSION 3.21)

# ---- Static CRT (must be set before project()) ----
# SKSE plugins must be self-contained; dynamic CRT causes 0x7E load failure.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- Project ----

project(
    ExperienceAndAttributes
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---- Options ----

set(SKYRIM_PATH "" CACHE PATH "Path to MO2 mod folder, e.g. C:/Modlist/NGVO/mods/Experience and Attributes")

# ---- CommonLibSSE-NG setup ----

set(SKSE_SUPPORT_XBYAK ON)

# Use Address Library (AE mode)
if(NOT DEFINED SKSE_TARGET_AE)
    set(SKSE_TARGET_AE ON)
endif()

add_subdirectory(extern/CommonLibSSE-NG)

include(cmake/CommonLibSSE.cmake)

# ---- vcpkg / find packages ----

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ---- Source files ----

set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/XPManager.cpp
    src/SkillHook.cpp
    src/SkillMenu.cpp
    src/EventSinks.cpp
)

# ---- Build ----

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "EA"
    VERSION ${PROJECT_VERSION}
    USE_ADDRESS_LIBRARY
    SOURCES ${SOURCES}
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

# Disable warnings as errors so MSVC warnings don't block the build
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /WX->
)

# ---- Post-build: copy DLL to mod folder ----

if(SKYRIM_PATH)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${SKYRIM_PATH}/SKSE/Plugins/${PROJECT_NAME}.dll"
        COMMENT "Copying DLL to mod folder..."
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json"
            "${SKYRIM_PATH}/SKSE/Plugins/${PROJECT_NAME}.json"
        COMMENT "Copying JSON config to mod folder..."
    )
endif()
