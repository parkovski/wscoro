find_package(Catch2 3.0 REQUIRED)

set(SOURCES
  test.cpp
  task.cpp
  generator.cpp
  suspend.cpp
  #lifecycle/lifecycle.cpp
)

add_executable(test ${SOURCES})
target_link_libraries(test Catch2::Catch2 wscoro)

include(CTest)
include(Catch)
catch_discover_tests(test)
