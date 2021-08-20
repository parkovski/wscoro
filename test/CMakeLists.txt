find_package(Catch2 3.0 REQUIRED)

set(SOURCES
  basic.cpp
  generator.cpp
  suspend.cpp
  continue.cpp
)

add_executable(test ${SOURCES})
target_link_libraries(test Catch2::Catch2WithMain wscoro)

include(CTest)
include(Catch)
catch_discover_tests(test)
