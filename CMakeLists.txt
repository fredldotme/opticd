cmake_minimum_required(VERSION 3.0.0)

project(opticd LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem -I/usr/include")

find_package(Qt5 REQUIRED Core DBus)
find_package(PkgConfig)

add_executable(opticd
  src/eglhelper.h
  src/eglhelper.cpp
  src/hybriscamerasource.h
  src/hybriscamerasource.cpp
  src/v4l2loopbacksink.h
  src/v4l2loopbacksink.cpp
  src/main.cpp
)

target_link_libraries(opticd Qt5::Core Qt5::DBus camera cap EGL GLESv2)

install(TARGETS opticd RUNTIME DESTINATION bin)
install(FILES aux/service/opticd.conf DESTINATION share/upstart/sessions)
install(FILES aux/udev/50-opticd.rules DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/udev/rules.d)
