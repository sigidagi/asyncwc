include_guard(GLOBAL)

if(NOT ASYNCWC_ENABLE_COVERAGE)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "ASYNCWC_ENABLE_COVERAGE requires GCC or Clang")
endif()

add_compile_options(--coverage -O0 -g -fno-inline)
add_link_options(--coverage)

find_program(LCOV_EXE lcov REQUIRED)
find_program(GENHTML_EXE genhtml REQUIRED)

# lcov needs the gcov flavour that matches the compiler.
get_filename_component(_asyncwc_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    find_program(LLVM_COV_EXE llvm-cov HINTS "${_asyncwc_compiler_dir}" NO_DEFAULT_PATH)
    find_program(LLVM_COV_EXE llvm-cov REQUIRED)
    set(ASYNCWC_GCOV_TOOL "${PROJECT_BINARY_DIR}/llvm-gcov.sh")
    file(WRITE "${ASYNCWC_GCOV_TOOL}" "#!/bin/sh\nexec \"${LLVM_COV_EXE}\" gcov \"$@\"\n")
    file(CHMOD "${ASYNCWC_GCOV_TOOL}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
else()
    string(REGEX MATCH "^[0-9]+" _asyncwc_gcc_major "${CMAKE_CXX_COMPILER_VERSION}")
    find_program(ASYNCWC_GCOV_TOOL NAMES gcov-${_asyncwc_gcc_major} gcov
        HINTS "${_asyncwc_compiler_dir}" REQUIRED)
endif()

# lcov 2.x is strict about the line data GCC emits for coroutine frames. The "line" filter
# drops gcov artifacts: zero-count closing braces and blank lines that stand for
# compiler-generated exception cleanup code.
set(_asyncwc_lcov_ignore --ignore-errors mismatch,inconsistent,unused,negative --filter line,range)

add_custom_target(coverage
    COMMAND "${LCOV_EXE}" --gcov-tool "${ASYNCWC_GCOV_TOOL}" --zerocounters
            --directory "${PROJECT_BINARY_DIR}" ${_asyncwc_lcov_ignore}
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure
    COMMAND "${LCOV_EXE}" --gcov-tool "${ASYNCWC_GCOV_TOOL}" --capture
            --directory "${PROJECT_BINARY_DIR}"
            --include "${PROJECT_SOURCE_DIR}/src/*"
            --include "${PROJECT_SOURCE_DIR}/include/*"
            --output-file coverage.info ${_asyncwc_lcov_ignore}
    COMMAND "${GENHTML_EXE}" coverage.info --output-directory coverage
            --title "asyncwc" ${_asyncwc_lcov_ignore}
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    DEPENDS asyncwc_tests
    COMMENT "Collecting coverage into ${PROJECT_BINARY_DIR}/coverage/index.html"
    VERBATIM)
