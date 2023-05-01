cmake_minimum_required(VERSION 3.15.0)
project(ShairportQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# locate packages
if (UNIX)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets DBus)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets DBus)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets})
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()

find_package(OpenSSL REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(sockpp CONFIG REQUIRED)

# Shairport Library
set(LIBRARY_SOURCES lib/dnssd.cpp lib/RaopServer.cpp lib/LayerCake.cpp lib/base64.cpp 
        lib/crypto.cpp lib/HairTunes.cpp lib/libutils.cpp lib/RaopEndpoint.cpp
        lib/audio/AlsaAudio.cpp lib/audio/AudioPlayer.cpp lib/SuspendInhibitor.cpp lib/Networking.cpp
        lib/DmapParser.cpp lib/KeyboardHook.cpp)

add_library(ShairLib STATIC ${LIBRARY_SOURCES})
if (MSVC)
    # we apply the static CRT for MSVC
    set_property(TARGET ShairLib PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # dismiss several warnings
    target_compile_options(ShairLib PRIVATE -wd4996)
else()
    # dismiss several warnings
    target_compile_options(ShairLib PRIVATE -Wno-deprecated-declarations)
endif()
target_include_directories(ShairLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
target_include_directories(ShairLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib/Bonjour")
target_link_libraries(ShairLib PRIVATE spdlog::spdlog spdlog::spdlog_header_only)
target_include_directories(ShairLib PRIVATE ${SOCKPP_INCLUDE_DIRS})
if (UNIX)
    target_link_libraries(ShairLib PRIVATE Qt${QT_VERSION_MAJOR}::DBus)
endif()

# Shairport Application
set(LOCALIZATION_SOURCES    app/localization/LanguageManager.cpp 
                            app/localization/StringIDs.cpp
                            app/localization/English.cpp
                            app/localization/Japanese.cpp
                            app/localization/German.cpp)

set(APP_SOURCES app/main.cpp
                app/MainDlg.cpp
                app/DacpService.cpp
                app/TimeLabel.cpp
                ${LOCALIZATION_SOURCES})

set(APP_RESOURCE res/resource.qrc)
if (MSVC)
    # include MSVS's native resource file
    set(APP_RESOURCE app/ShairportQT.rc ${APP_RESOURCE})
endif()

add_executable(ShairportQt ${APP_SOURCES} ${APP_RESOURCE})

if (MSVC)
    # we apply the static CRT for MSVC
    set_property(TARGET ShairportQt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if (CMAKE_BUILD_TYPE MATCHES ".*Rel.*")
        # this magic setting makes our app native to Windows (just for Release builds)
        # otherwise we'll create a "console" app
        set_target_properties(ShairportQt PROPERTIES WIN32_EXECUTABLE ON)
    endif()
    # dismiss several warnings
    target_compile_options(ShairportQt PRIVATE -wd4996)
else()
    # dismiss several warnings
    target_compile_options(ShairportQt PRIVATE -Wno-deprecated-declarations -Wnarrowing)
endif()

target_link_libraries(ShairportQt PRIVATE ShairLib)
if (UNIX)
    target_link_libraries(ShairportQt PRIVATE asound)
endif()
target_link_libraries(ShairportQt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(ShairportQt PRIVATE spdlog::spdlog spdlog::spdlog_header_only)
target_link_libraries(ShairportQt PRIVATE Sockpp::sockpp-static)
if (UNIX)
    target_link_libraries(ShairportQt PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::DBus)
else()
    target_link_libraries(ShairportQt PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
endif()

if (NOT(CMAKE_BUILD_TYPE MATCHES ".*MinSize.*") AND NOT(CMAKE_BUILD_TYPE MATCHES ".*RelWith.*"))
    # Unit testing
    include(CTest)
    enable_testing()

    set(TEST_SOURCES    test/main.cpp 
                        test/StreamTest.cpp 
                        test/EndpointTest.cpp 
                        test/TrimTest.cpp
                        test/QueueTest.cpp
                        test/NetworkingTest.cpp)

    add_executable(ShairportQtTest ${TEST_SOURCES})

    if (MSVC)
        # we apply the dynamic CRT for MSVC
        set_property(TARGET ShairportQtTest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        # dismiss several warnings
        target_compile_options(ShairportQtTest PRIVATE -wd4996)
    else()
        # dismiss several warnings
        target_compile_options(ShairportQtTest PRIVATE -Wno-deprecated-declarations)
    endif()
    target_link_libraries(ShairportQtTest PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
    target_link_libraries(ShairportQtTest PRIVATE ShairLib)
    target_link_libraries(ShairportQtTest PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_link_libraries(ShairportQtTest PRIVATE Sockpp::sockpp-static)

    add_test(AllTests ShairportQtTest)
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

if (QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(ShairportQt)
endif()