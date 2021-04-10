cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)

project(libwscoro)
set(libwscoro_VERSION_MAJOR 0)
set(libwscoro_VERSION_MINOR 1)
set(libwscoro_VERSION_PATCH 0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LIBWSCORO_BUILD_TESTS "Build tests" ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(WIN32)
    add_compile_options(-fcolor-diagnostics -Xclang -stdlib=libc++)
    add_link_options(-lc++ -lc++experimental -lc++abi)
  else()
    add_compile_options(-fcolor-diagnostics -stdlib=libc++)
    add_link_options(-lc++ -lc++experimental -lc++abi)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-fcoroutines)
endif()

find_package(spdlog REQUIRED)

add_library(libwscoro INTERFACE)
target_link_libraries(libwscoro INTERFACE spdlog::spdlog_header_only)
target_include_directories(libwscoro INTERFACE ${PROJECT_SOURCE_DIR}/include)

if(LIBWSCORO_BUILD_TESTS)
  add_subdirectory(test)
endif()