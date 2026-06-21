cmake_minimum_required(VERSION 3.16)
project(pipeline_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pipeline_sim
    src/main.cpp
    src/processor.cpp
    src/reservation_station.cpp
    src/register_alias_table.cpp
    src/reorder_buffer.cpp
    src/instruction_fetch_queue.cpp
    src/execution_engine.cpp
    src/load_store_queue.cpp
    src/return_address_stack.cpp
    src/gshare_predictor.cpp
)

target_include_directories(pipeline_sim PRIVATE include)
