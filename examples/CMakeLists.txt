function(add_nozzle_example NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE
        "-Wl,-force_load"
        nozzle
    )
    target_include_directories(${NAME} PRIVATE
        ${plog_SOURCE_DIR}/include
    )
    target_compile_options(${NAME} PRIVATE
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-exceptions -fno-rtti>
        $<$<CXX_COMPILER_ID:GNU>:-fno-exceptions -fno-rtti>
    )
endfunction()

add_nozzle_example(example_sender)
add_nozzle_example(example_receiver)
