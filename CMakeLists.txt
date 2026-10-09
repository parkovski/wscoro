cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)

project(wscoro)
set(wscoro_VERSION_MAJOR 0)
set(wscoro_VERSION_MINOR 3)
set(wscoro_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  # Subproject
  set(BUILD_TESTING OFF)
endif()
option(BUILD_TESTING "Build tests" ON)

add_library(wscoro INTERFACE)
target_include_directories(wscoro INTERFACE ${PROJECT_SOURCE_DIR}/include)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()
