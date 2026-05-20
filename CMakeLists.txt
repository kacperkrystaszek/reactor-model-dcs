cmake_minimum_required(VERSION 3.10)
project(FlowShop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

include_directories(include)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set(SOURCES
    src/config/ConfigLoader.cpp
    src/messages/MessageConstructor.cpp
    src/ReactorModel.cpp
    src/UDPSocket.cpp
    src/main.cpp
)

set(CONTROLLER_SOURCES
    src/config/ConfigLoader.cpp
    src/messages/MessageConstructor.cpp
    src/UDPSocket.cpp
    src/Controller.cpp
)

add_executable(main ${SOURCES})
find_package(Threads REQUIRED)

add_executable(feed_controller src/feed_main.cpp ${CONTROLLER_SOURCES})
target_link_libraries(feed_controller PRIVATE Threads::Threads)
target_compile_definitions(feed_controller PRIVATE DISABLE_RT_LOGGING)

add_executable(coolant_controller src/coolant_main.cpp ${CONTROLLER_SOURCES})
target_link_libraries(coolant_controller PRIVATE Threads::Threads)
target_compile_definitions(coolant_controller PRIVATE DISABLE_RT_LOGGING)