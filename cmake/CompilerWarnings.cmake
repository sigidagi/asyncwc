include_guard(GLOBAL)

# Applies the project's warning set to `target`.
# (-Wnull-dereference is left out: with optimisation it reports false positives from
# inlined Boost.Asio/Beast internals.)
function(asyncwc_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-
            $<$<BOOL:${ASYNCWC_WARNINGS_AS_ERRORS}>:/WX>)
        return()
    endif()

    set(warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND warnings
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op)
    endif()

    if(ASYNCWC_WARNINGS_AS_ERRORS)
        list(APPEND warnings -Werror)
    endif()

    target_compile_options(${target} PRIVATE ${warnings})
endfunction()
