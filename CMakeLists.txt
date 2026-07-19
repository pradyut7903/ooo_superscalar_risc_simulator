cmake_minimum_required(VERSION 3.16)
project(pipeline_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The installed experimental GCC 16.1.0 miscompiles this code at -O1/-O2 (a
# layout-dependent teardown crash). Force -O0; the simulator is small and fast
# enough that optimization is unnecessary. See AGENT_CONTEXT.md / EXPERIMENTS.md.
add_compile_options(-O0)

add_executable(pipeline_sim
    src/main.cpp
    src/processor.cpp
    src/reservation_station.cpp
    src/register_alias_table.cpp
    src/reorder_buffer.cpp
    src/instruction_fetch_queue.cpp
    src/execution_engine.cpp
    src/cdb_arbiter.cpp
    src/decode.cpp
    src/dispatch_reg.cpp
    src/hex_loader.cpp
    src/rat_checkpoints.cpp
    src/load_store_queue.cpp
    src/return_address_stack.cpp
    src/gshare_predictor.cpp
    src/trace_loader.cpp
    src/mem/dram_simple.cpp
    src/mem/dcache.cpp
    src/mem/icache.cpp
    src/mem/memory_system.cpp
)
target_include_directories(pipeline_sim PRIVATE include)

# Stage 2: decode + executeOp directed checks (not part of the cycle pipeline yet).
add_executable(tb_decode_ee
    tests/tb_decode_ee.cpp
    src/decode.cpp
    src/execution_engine.cpp
    src/cdb_arbiter.cpp
)
target_include_directories(tb_decode_ee PRIVATE include)

# Stage 4: dispatch_reg prefix-accept unit test.
add_executable(tb_dispatch_reg
    tests/tb_dispatch_reg.cpp
    src/dispatch_reg.cpp
)
target_include_directories(tb_dispatch_reg PRIVATE include)

# Golden reference model (in-order ISS) used for functional co-simulation.
add_executable(golden_sim
    golden/golden_main.cpp
    golden/golden_model.cpp
    src/trace_loader.cpp
)
target_include_directories(golden_sim PRIVATE include)
