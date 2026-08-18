cmake_minimum_required(VERSION 3.16)
project(cliforge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# cliforge is header-only.
add_library(cliforge INTERFACE)
target_include_directories(cliforge INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(packctl examples/main.cpp)
target_link_libraries(packctl PRIVATE cliforge)
target_compile_options(packctl PRIVATE -Wall -Wextra)

enable_testing()

add_executable(type_test tests/type_test.cpp)
target_link_libraries(type_test PRIVATE cliforge)
target_compile_options(type_test PRIVATE -Wall -Wextra)

add_executable(registration_errors_test tests/registration_errors_test.cpp)
target_link_libraries(registration_errors_test PRIVATE cliforge)
target_compile_options(registration_errors_test PRIVATE -Wall -Wextra)

add_test(NAME registration_errors COMMAND registration_errors_test)
