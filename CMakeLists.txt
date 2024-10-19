cmake_minimum_required(VERSION 3.16.0)
project(async_mqtt5_test)

find_package(Boost REQUIRED COMPONENTS headers)
find_package(async-mqtt5 REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(sender sender.cpp)
target_link_libraries(sender PRIVATE Async::MQTT5 Boost::headers OpenSSL::SSL)

add_executable(receiver receiver.cpp)
target_link_libraries(receiver PRIVATE Async::MQTT5 Boost::headers OpenSSL::SSL)
