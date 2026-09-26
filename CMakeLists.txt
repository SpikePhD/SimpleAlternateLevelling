cmake_minimum_required(VERSION 3.21)

# The x64-windows-static-md triplet links third-party libraries statically while
# retaining the /MD runtime required by Skyrim and CommonLibSSE-NG.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(
    SimpleAlternateLevelling
    VERSION 0.4.0
    LANGUAGES CXX
)

option(SAL_BUILD_PLUGIN "Build the Skyrim SKSE plugin" ON)
option(BUILD_TESTS "Build dependency-free progression, reward, logging, and UI tests" OFF)
set(SAL_DEPLOY_DIR "" CACHE PATH "Optional MO2 mod root for post-build deployment")
set(SKYRIM_PATH "" CACHE PATH "Deprecated alias for SAL_DEPLOY_DIR")
set(SAL_JAVA_EXECUTABLE "" CACHE FILEPATH "Java 17 executable used only by skill-menu SWF targets")
set(SAL_FFDEC_JAR "" CACHE FILEPATH "FFDec 25.1.3 jar used only by skill-menu SWF targets")

if(SKYRIM_PATH)
    if(SAL_DEPLOY_DIR AND NOT SAL_DEPLOY_DIR STREQUAL SKYRIM_PATH)
        message(FATAL_ERROR "SAL_DEPLOY_DIR and deprecated SKYRIM_PATH specify different directories.")
    endif()
    if(NOT SAL_DEPLOY_DIR)
        set(SAL_DEPLOY_DIR "${SKYRIM_PATH}" CACHE PATH "Optional MO2 mod root for post-build deployment" FORCE)
    endif()
    message(DEPRECATION "SKYRIM_PATH is deprecated; use SAL_DEPLOY_DIR instead.")
endif()

if(SAL_BUILD_PLUGIN)
    if(NOT WIN32)
        message(FATAL_ERROR "The SKSE plugin can only be built on Windows. Use -DSAL_BUILD_PLUGIN=OFF for portable tests.")
    endif()
    if(DEFINED VCPKG_TARGET_TRIPLET AND
       NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-static-md")
        message(FATAL_ERROR
            "SimpleAlternateLevelling requires VCPKG_TARGET_TRIPLET=x64-windows-static-md; "
            "received '${VCPKG_TARGET_TRIPLET}'.")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG/CMakeLists.txt")
        message(FATAL_ERROR
            "CommonLibSSE-NG is missing. Clone with --recurse-submodules or run "
            "'git submodule update --init --recursive'.")
    endif()

    set(SKSE_SUPPORT_XBYAK ON)
    set(ENABLE_SKYRIM_SE ON)
    set(ENABLE_SKYRIM_AE ON)
    set(ENABLE_SKYRIM_VR OFF)

    # CommonLibSSE-NG uses BUILD_TESTS for its own dependency-heavy suite.
    # Disable that suite without changing this project's public option.
    set(SAL_REQUESTED_BUILD_TESTS ${BUILD_TESTS})
    set(BUILD_TESTS OFF)
    add_subdirectory(extern/CommonLibSSE-NG)
    set(BUILD_TESTS ${SAL_REQUESTED_BUILD_TESTS})

    include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(SAL_SWF_TOOL_ARGS
        --java "${SAL_JAVA_EXECUTABLE}"
        --ffdec-jar "${SAL_FFDEC_JAR}"
        --swf "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/EA_SkillMenu.swf"
        --scripts "${CMAKE_CURRENT_SOURCE_DIR}/assets/swf_src/scripts"
        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/swf"
    )
    add_custom_target(rebuild_skill_menu
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/skill_menu_swf.py"
            rebuild ${SAL_SWF_TOOL_ARGS}
        COMMENT "Rebuilding EA_SkillMenu.swf with FFDec 25.1.3"
        VERBATIM
    )
    add_custom_target(verify_skill_menu
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/skill_menu_swf.py"
            verify ${SAL_SWF_TOOL_ARGS}
        COMMENT "Verifying EA_SkillMenu.swf against tracked ActionScript"
        VERBATIM
    )

    set(SAL_PLUGIN_SOURCES
        src/main.cpp
        src/Config.cpp
        src/SettingsModel.cpp
        src/XPManager.cpp
        src/Progression.cpp
        src/RewardRules.cpp
        src/LogPolicy.cpp
        src/UIRules.cpp
        src/Leveling.cpp
        src/Integration.cpp
        src/SkillHook.cpp
        src/SkillMenu.cpp
        src/SettingsPage.cpp
        src/InfoPages.cpp
        src/UIText.cpp
        src/XPJournal.cpp
        src/EventSinks.cpp
    )

    add_commonlibsse_plugin(${PROJECT_NAME}
        NAME ${PROJECT_NAME}
        AUTHOR "EA"
        VERSION ${PROJECT_VERSION}
        USE_ADDRESS_LIBRARY
        STRUCT_DEPENDENT
        SOURCES ${SAL_PLUGIN_SOURCES}
    )

    # CommonLib's CMake helper marks struct compatibility as dependent but its
    # generated RuntimeCompatibility initializer leaves SKSE's post-1.6.629
    # structure-layout bit unset. SKSE 2.3.1 rejects that combination with
    # "only compatible with versions earlier than 1.6.629". Set both the
    # Address Library and post-629 layout flags in the exported metadata.
    set(SAL_PLUGIN_METADATA_FILE "${CMAKE_CURRENT_BINARY_DIR}/__${PROJECT_NAME}Plugin.cpp")
    file(READ "${SAL_PLUGIN_METADATA_FILE}" SAL_PLUGIN_METADATA)
    set(SAL_UNPATCHED_RUNTIME_METADATA
        ".RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,")
    set(SAL_PATCHED_RUNTIME_METADATA
        ".RuntimeCompatibility = { SKSE::VersionIndependence::AddressLibrary, true },")
    string(FIND "${SAL_PLUGIN_METADATA}" "${SAL_UNPATCHED_RUNTIME_METADATA}" SAL_UNPATCHED_RUNTIME_METADATA_OFFSET)
    if(SAL_UNPATCHED_RUNTIME_METADATA_OFFSET EQUAL -1)
        message(FATAL_ERROR "Unexpected CommonLib plugin metadata; cannot declare post-1.6.629 structure compatibility.")
    endif()
    string(REPLACE "${SAL_UNPATCHED_RUNTIME_METADATA}" "${SAL_PATCHED_RUNTIME_METADATA}"
        SAL_PLUGIN_METADATA "${SAL_PLUGIN_METADATA}")
    string(APPEND SAL_PLUGIN_METADATA
        "\nconstexpr SKSE::PluginDeclaration::RuntimeCompatibility kSALRuntimeCompatibility{\n"
        "    SKSE::VersionIndependence::AddressLibrary, true\n"
        "};\nstatic_assert(kSALRuntimeCompatibility.Targets629Structs());\n")
    file(WRITE "${SAL_PLUGIN_METADATA_FILE}" "${SAL_PLUGIN_METADATA}")

    target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
    target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/SKSEMenuFramework)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SAL_VERSION="${PROJECT_VERSION}")
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /WX->
    )

    if(SAL_DEPLOY_DIR)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${SAL_DEPLOY_DIR}/SKSE/Plugins"
                "${SAL_DEPLOY_DIR}/Interface"
                "${SAL_DEPLOY_DIR}/Interface/Translations"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${PROJECT_NAME}>
                "${SAL_DEPLOY_DIR}/SKSE/Plugins/${PROJECT_NAME}.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json"
                "${SAL_DEPLOY_DIR}/SKSE/Plugins/${PROJECT_NAME}.json"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/EA_SkillMenu.swf"
                "${SAL_DEPLOY_DIR}/Interface/EA_SkillMenu.swf"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/Translations/SimpleAlternateLevelling_ENGLISH.txt"
                "${SAL_DEPLOY_DIR}/Interface/Translations/SimpleAlternateLevelling_ENGLISH.txt"
            COMMENT "Deploying plugin files to ${SAL_DEPLOY_DIR}"
            VERBATIM
        )
    endif()

    set(SAL_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Deployed")
    add_custom_target(package_mod
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_mod.py"
            --dll $<TARGET_FILE:${PROJECT_NAME}>
            --config "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json"
            --swf "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/EA_SkillMenu.swf"
            --translation "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/Translations/SimpleAlternateLevelling_ENGLISH.txt"
            --output-dir "${SAL_PACKAGE_DIR}"
            --version "${PROJECT_VERSION}"
            --runtime "SE-AE"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating deterministic SimpleAlternateLevelling mod package"
        VERBATIM
    )
endif()

if(BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    add_executable(SimpleAlternateLevellingTests
        tests/ProgressionTests.cpp
        src/Progression.cpp
    )
    target_compile_features(SimpleAlternateLevellingTests PRIVATE cxx_std_23)
    target_include_directories(SimpleAlternateLevellingTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME progression_integrity COMMAND SimpleAlternateLevellingTests)

    add_executable(SimpleAlternateLevellingRewardTests
        tests/RewardRulesTests.cpp
        src/RewardRules.cpp
    )
    target_compile_features(SimpleAlternateLevellingRewardTests PRIVATE cxx_std_23)
    target_include_directories(SimpleAlternateLevellingRewardTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME reward_event_rules COMMAND SimpleAlternateLevellingRewardTests)

    add_executable(SimpleAlternateLevellingIntegrationTests
        tests/IntegrationRulesTests.cpp
    )
    target_compile_features(SimpleAlternateLevellingIntegrationTests PRIVATE cxx_std_23)
    target_include_directories(SimpleAlternateLevellingIntegrationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME integration_rules COMMAND SimpleAlternateLevellingIntegrationTests)

    add_executable(SimpleAlternateLevellingLogPolicyTests
        tests/LogPolicyTests.cpp
        src/LogPolicy.cpp
    )
    target_compile_features(SimpleAlternateLevellingLogPolicyTests PRIVATE cxx_std_23)
    target_include_directories(SimpleAlternateLevellingLogPolicyTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME log_retention_policy COMMAND SimpleAlternateLevellingLogPolicyTests)

    add_executable(SimpleAlternateLevellingUIRulesTests
        tests/UIRulesTests.cpp
        src/UIRules.cpp
    )
    target_compile_features(SimpleAlternateLevellingUIRulesTests PRIVATE cxx_std_23)
    target_include_directories(SimpleAlternateLevellingUIRulesTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME ui_defensive_rules COMMAND SimpleAlternateLevellingUIRulesTests)

    add_test(NAME ui_asset_contract
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_ui_assets.py"
            --native "${CMAKE_CURRENT_SOURCE_DIR}/src/SkillMenu.cpp"
            --actionscript "${CMAKE_CURRENT_SOURCE_DIR}/assets/swf_src/scripts/frame_1/DoAction.as"
            --translation "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/Translations/SimpleAlternateLevelling_ENGLISH.txt"
            --swf "${CMAKE_CURRENT_SOURCE_DIR}/data/Interface/EA_SkillMenu.swf"
            --defaults "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json")
    add_test(NAME release_defaults
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_release_defaults.py"
            --defaults "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json")
    if(SAL_BUILD_PLUGIN)
        add_executable(SimpleAlternateLevellingSettingsTests
            tests/SettingsModelTests.cpp src/SettingsModel.cpp src/Progression.cpp)
        target_compile_features(SimpleAlternateLevellingSettingsTests PRIVATE cxx_std_23)
        target_include_directories(SimpleAlternateLevellingSettingsTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(SimpleAlternateLevellingSettingsTests PRIVATE nlohmann_json::nlohmann_json)
        add_test(NAME settings_model COMMAND SimpleAlternateLevellingSettingsTests
            "${CMAKE_CURRENT_SOURCE_DIR}/data/SKSE/Plugins/${PROJECT_NAME}.json")
    endif()
endif()
