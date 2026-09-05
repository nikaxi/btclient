cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Debug")

project(btclient)

set(GTEST_ROOT /home/nikaxi/cpp/googletest)

# Include GTest and GMock
include_directories(${GTEST_ROOT}/googletest/include)
include_directories(${GTEST_ROOT}/googlemock/include)

# Link with GTest and GMock libraries
link_directories(${GTEST_ROOT}/build/lib)
# add_executable(message_test message.cpp message_test.cpp)
add_executable(utils_test utils_test.cpp)
add_executable(main main.cpp client.cpp torrent.cpp)
# target_link_libraries(message_test gtest gtest_main gmock gmock_main)
target_link_libraries(utils_test gtest gtest_main gmock gmock_main ssl crypto)

target_link_libraries(main ssl crypto)

# Enable testing
enable_testing()
add_test(NAME MessageTests COMMAND message_test)