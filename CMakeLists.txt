cmake_minimum_required(VERSION 3.20)

project(seccam LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SECCAM_BUILD_CORE_DAEMON "Build the new production-oriented core daemon" ON)

find_package(Threads REQUIRED)

if(SECCAM_BUILD_CORE_DAEMON)
  add_subdirectory(core)
endif()
