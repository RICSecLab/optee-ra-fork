cmake_minimum_required(VERSION 3.10)
project (optee_remote_attestation C)

# Option to build with or without Veraison client
option(MOCK_ONLY "Build without Veraison client (for QEMU testing without server)" OFF)

if(MOCK_ONLY)
    add_compile_definitions(MOCK_ONLY=1)
    set(SRC host/main.c)
else()
    set(SRC host/main.c host/client.c)
    # Rust FFI static library path
    set(RUST_FFI_LIB ${CMAKE_CURRENT_SOURCE_DIR}/host/rust-ffi/target/aarch64-unknown-linux-gnu/release/libveraison_client_ffi.a)
endif()

add_executable(${PROJECT_NAME} ${SRC})

target_include_directories(${PROJECT_NAME}
                           PRIVATE ta/include
                           PRIVATE host)

target_compile_definitions(${PROJECT_NAME} PRIVATE HOST_BUILD=1)

if(MOCK_ONLY)
    target_link_libraries(${PROJECT_NAME} PRIVATE teec m)
else()
    # Link with Rust FFI library and its dependencies
    target_link_libraries(${PROJECT_NAME} PRIVATE
        teec
        m
        ${RUST_FFI_LIB}
        pthread
        dl
    )
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
