cmake_minimum_required(VERSION 3.21)
project(TBLIBAutoBuild VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not set. Install Geode SDK 5.8.2 and set GEODE_SDK.")
endif()

find_package(Geode REQUIRED)

add_geode_mod(TBLIBAutoBuild
    src/main.cpp
)
