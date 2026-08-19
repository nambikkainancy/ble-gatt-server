cmake_minimum_required(VERSION 3.13)
project(ble-gatt-server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED gio-2.0 gio-unix-2.0 glib-2.0)

add_executable(ble-gatt-server src/main.c)

target_include_directories(ble-gatt-server PRIVATE ${GIO_INCLUDE_DIRS})
target_link_libraries(ble-gatt-server PRIVATE ${GIO_LIBRARIES})
target_compile_options(ble-gatt-server PRIVATE -Wall -Wextra ${GIO_CFLAGS_OTHER})
