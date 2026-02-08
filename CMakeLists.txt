cmake_minimum_required(VERSION 3.10)
project(NovaCommand)

set(CMAKE_CXX_STANDARD 17)

# Find libgphoto2
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPHOTO2 REQUIRED libgphoto2)

include_directories(${GPHOTO2_INCLUDE_DIRS})
link_directories(${GPHOTO2_LIBRARY_DIRS})

add_executable(nova_command main.cpp)
target_link_libraries(nova_command ${GPHOTO2_LIBRARIES})
