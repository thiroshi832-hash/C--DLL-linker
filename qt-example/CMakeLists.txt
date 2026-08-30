cmake_minimum_required(VERSION 3.16)
project(qt_clr_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt5 or Qt6, whichever your MinGW kit provides.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

add_executable(qt_clr_demo
    main.cpp
    ClrBridge.cpp
    ClrBridge.h
)

# Path to qtclrbridge.h so the wrapper can see the ABI (optional; QLibrary
# resolves symbols at runtime, but the header documents the contract).
target_include_directories(qt_clr_demo PRIVATE ${CMAKE_SOURCE_DIR}/../native-bridge)

target_link_libraries(qt_clr_demo PRIVATE Qt${QT_VERSION_MAJOR}::Core)
