cmake_minimum_required(VERSION 3.10)
project(HighPerfWebServer_Plus)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

add_executable(server
    main.cpp
    src/Buffer.cpp
    src/Epoll.cpp
    src/HttpConnection.cpp
    src/HttpRequest.cpp
    src/IdleTimerManager.cpp
    src/InetAddress.cpp
    src/ServerLogger.cpp
    src/Socket.cpp
    src/SubReactor.cpp
)

target_compile_options(server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
