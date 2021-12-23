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
  src/accessmediator.h
  src/accessmediator.cpp
  src/eglhelper.h
  src/eglhelper.cpp
  src/hybriscamerasource.h
  src/hybriscamerasource.cpp
  src/v4l2loopbacksink.h
  src/v4l2loopbacksink.cpp
  src/main.cpp
)

target_link_libraries(opticd Qt5::Core camera cap EGL GLESv2 ${MIRCLIENT_LDFLAGS})

install(TARGETS opticd RUNTIME DESTINATION bin)
install(FILES aux/service/opticd.conf DESTINATION share/upstart/sessions)
install(FILES aux/sysctl/50-opticd.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/sysctl.d)
