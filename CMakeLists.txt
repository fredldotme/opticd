cmake_minimum_required(VERSION 3.0.0)

project(opticd LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem -I/usr/include")

find_package(Qt5 REQUIRED Core)
find_package(PkgConfig)
pkg_check_modules(MIRCLIENT REQUIRED mirclient)

include_directories(
  ${MIRCLIENT_INCLUDE_DIRS}
)

add_executable(opticd
  accessmediator.h
  accessmediator.cpp
  eglhelper.h
  eglhelper.cpp
  hybriscamerasource.h
  hybriscamerasource.cpp
  v4l2loopbacksink.h
  v4l2loopbacksink.cpp
  main.cpp
)

target_link_libraries(opticd Qt5::Core camera EGL GLESv2 ${MIRCLIENT_LDFLAGS})
