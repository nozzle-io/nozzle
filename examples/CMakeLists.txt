function(add_nozzle_example NAME)
    add_executable(${NAME} ${NAME}.cpp)
    if(APPLE)
        target_link_libraries(${NAME} PRIVATE
            "-Wl,-force_load"
            nozzle
        )
    else()
        target_link_libraries(${NAME} PRIVATE
            nozzle
        )
    endif()
    target_include_directories(${NAME} PRIVATE
        ${plog_SOURCE_DIR}/include
    )
    target_compile_options(${NAME} PRIVATE
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-rtti>
        $<$<CXX_COMPILER_ID:GNU>:-fno-rtti>
    )
    if(NOZZLE_STRICT_NO_EXCEPTIONS)
        target_compile_options(${NAME} PRIVATE
            $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-exceptions>
            $<$<CXX_COMPILER_ID:GNU>:-fno-exceptions>
        )
    endif()
endfunction()

add_nozzle_example(example_sender)
add_nozzle_example(example_receiver)
