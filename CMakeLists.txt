cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)

project(wscoro)
set(wscoro_VERSION_MAJOR 0)
set(wscoro_VERSION_MINOR 1)
set(wscoro_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  # Subproject
  set(WSCORO_BUILD_TESTS OFF)
endif()
option(WSCORO_BUILD_TESTS "Build tests" ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(WIN32)
    add_compile_options(-Xclang -stdlib=libc++)
    add_link_options(-lc++experimental -lc++abi)
  else()
    add_compile_options(-stdlib=libc++)
    add_link_options(-lc++experimental -lc++abi)
  endif()

  if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    if((NOT CMAKE_BUILD_TYPE) OR (CMAKE_BUILD_TYPE MATCHES "Deb"))
      add_compile_options(-ggdb)
    endif()
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-fcoroutines)
endif()

find_package(spdlog REQUIRED)

add_library(wscoro INTERFACE)
target_link_libraries(wscoro INTERFACE spdlog::spdlog_header_only)
target_include_directories(wscoro INTERFACE ${PROJECT_SOURCE_DIR}/include)

if(WSCORO_BUILD_TESTS)
  add_subdirectory(test)
endif()