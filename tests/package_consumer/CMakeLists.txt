cmake_minimum_required(VERSION 3.15)
project(aervault_package_consumer LANGUAGES C)
find_package(aervault 0.1 CONFIG REQUIRED)

add_executable(core_only ../link_core.c)
target_link_libraries(core_only PRIVATE aervault::core)

add_executable(counter_only ../link_counter.c)
target_link_libraries(counter_only PRIVATE aervault::counter)
