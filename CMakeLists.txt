cmake_minimum_required(VERSION 3.10)
project(NovaCommand)

set(CMAKE_CXX_STANDARD 17)

# Find libusb
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

include_directories(${LIBUSB_INCLUDE_DIRS})
link_directories(${LIBUSB_LIBRARY_DIRS})

add_executable(nova_detect main.cpp)
target_link_libraries(nova_detect ${LIBUSB_LIBRARIES})
