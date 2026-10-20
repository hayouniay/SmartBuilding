cmake_minimum_required(VERSION 3.21)
project(SmartBuilding VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # lets VS Code / clangd find the flags

option(SB_BUILD_APPS "Build the Qt server, client and simulator" ON)
option(SB_BUILD_TESTS "Build the dependency-free unit tests" ON)

# Compiler-specific settings (GCC/Clang on Linux, MSVC on Windows).
if(MSVC)
    # The sources contain UTF-8 text such as "°C". MSVC reads source files as the
    # system code page unless /utf-8 is given, which would garble those strings.
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    # Stops <windows.h> from defining min/max macros that clash with std::min/std::max.
    add_compile_definitions(NOMINMAX)
endif()

# windeployqt flag: MSVC Qt packages ship Debug DLLs; MinGW Qt packages only ship Release DLLs.
if(MSVC)
    set(SB_DEPLOY_CONFIG_FLAG "$<IF:$<CONFIG:Debug>,--debug,--release>")
else()
    set(SB_DEPLOY_CONFIG_FLAG "--release")
endif()

# Shared protocol and rule code. Root dir is an include path so sources can write
# #include "common/Rules.h".
add_library(sb_common INTERFACE)
target_include_directories(sb_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(SB_BUILD_TESTS)
    enable_testing()
    add_executable(sb_unit_tests tests/test_core.cpp)
    target_link_libraries(sb_unit_tests PRIVATE sb_common)
    add_test(NAME sb_core COMMAND sb_unit_tests)
endif()

if(SB_BUILD_APPS)
    set(CMAKE_AUTOMOC ON)
    # Qt MQTT is built into deps/install by the install task (see deps/qtmqtt.env).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/install")
        list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/install")
    endif()
    find_package(Qt6 6.5 COMPONENTS Core Widgets Sql)
    # Qt MQTT is a separate package. Qt's own config files only search their own folder
    # for components, so Qt MQTT must be found on its own to be picked up from deps/install.
    find_package(Qt6Mqtt 6.5)
    if(NOT Qt6_FOUND OR NOT Qt6Mqtt_FOUND)
        message(FATAL_ERROR
            "Qt 6.5 or newer and the Qt MQTT module are required.\n"
            "Run the install task (builds Qt MQTT into deps/install), or pass\n"
            "  -DCMAKE_PREFIX_PATH=<Qt install>\n"
            "Or build without the apps: -DSB_BUILD_APPS=OFF")
    endif()
    target_link_libraries(sb_common INTERFACE Qt6::Core)

    # On Windows, windeployqt copies the Qt DLLs and plugins next to each executable.
    if(WIN32)
        get_filename_component(SB_QT_PREFIX "${Qt6Core_DIR}/../../.." ABSOLUTE)
        find_program(SB_WINDEPLOYQT NAMES windeployqt
            HINTS "${SB_QT_PREFIX}/bin" "${Qt6Core_DIR}/../../../bin")
        if(NOT SB_WINDEPLOYQT)
            message(WARNING "windeployqt not found; Qt DLLs will not be copied next to the executables.")
        endif()
    endif()

    function(sb_deploy_qt target)
        if(SB_WINDEPLOYQT)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${SB_WINDEPLOYQT}" ${SB_DEPLOY_CONFIG_FLAG}
                        --no-translations "$<TARGET_FILE:${target}>"
                COMMENT "Deploying Qt runtime for ${target}")
        endif()
    endfunction()

    add_executable(sb_server
        server/main.cpp
        server/MqttServer.h
        server/MqttServer.cpp
        server/DeviceManager.h
        server/DeviceManager.cpp
        server/DatabaseManager.h
        server/DatabaseManager.cpp
    )
    target_link_libraries(sb_server PRIVATE sb_common Qt6::Core Qt6::Mqtt Qt6::Sql)
    sb_deploy_qt(sb_server)

    add_executable(sb_client
        client/main.cpp
        client/MainWindow.h
        client/MainWindow.cpp
        client/MqttClient.h
        client/MqttClient.cpp
        client/Widgets.h
    )
    target_link_libraries(sb_client PRIVATE sb_common Qt6::Widgets Qt6::Mqtt)
    sb_deploy_qt(sb_client)

    add_executable(sb_simulator
        simulator/main.cpp
        simulator/SensorSimulator.h
        simulator/SensorSimulator.cpp
    )
    target_link_libraries(sb_simulator PRIVATE sb_common Qt6::Core Qt6::Mqtt)
    sb_deploy_qt(sb_simulator)
endif()
