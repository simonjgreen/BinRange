cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cmsis_dap_tcp_esp32)
target_compile_options(__idf_main PRIVATE "-include${CMAKE_CURRENT_LIST_DIR}/idf_compat.h")
# All TLS symbols live in this statically linked image; avoid GOT-based TLS
# relocations, which the IDF 5.4 RISC-V linker script intentionally discards.
target_compile_options(__idf_main PRIVATE "-ftls-model=local-exec")
