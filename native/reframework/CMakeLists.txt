cmake_minimum_required(VERSION 3.20)
project(onimusha_dualsense_reframework LANGUAGES CXX)

add_library(onimusha_dualsense SHARED OnimushaDualSense.cpp)
target_compile_features(onimusha_dualsense PRIVATE cxx_std_17)
target_include_directories(onimusha_dualsense PRIVATE "${REF_INCLUDE_DIR}")
set_target_properties(onimusha_dualsense PROPERTIES
    OUTPUT_NAME OnimushaDualSense
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/reframework/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/reframework/plugins")
if (MSVC)
    target_compile_options(onimusha_dualsense PRIVATE /W4 /EHsc)
else()
    target_compile_options(onimusha_dualsense PRIVATE -Wall -Wextra -Wpedantic)
endif()
if (WIN32)
    target_link_libraries(onimusha_dualsense PRIVATE hid setupapi ws2_32)
endif()
if (NOT MSVC)
    target_link_options(onimusha_dualsense PRIVATE -static-libgcc -static-libstdc++)
endif()
install(TARGETS onimusha_dualsense
    RUNTIME DESTINATION game/reframework/plugins
    LIBRARY DESTINATION game/reframework/plugins
    ARCHIVE DESTINATION game/reframework/plugins)
