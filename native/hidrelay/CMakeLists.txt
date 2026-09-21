cmake_minimum_required(VERSION 3.20)
project(onimusha_hidrelay C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(onimusha_hidrelay hidrelay.c)
target_compile_options(onimusha_hidrelay PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(onimusha_hidrelay PRIVATE hidapi-hidraw)
install(TARGETS onimusha_hidrelay RUNTIME DESTINATION host)
