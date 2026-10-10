include_guard(GLOBAL)

# Override with -DCLANG_FORMAT_EXE=... / -DCLANG_TIDY_EXE=... to pin a version.
find_program(CLANG_FORMAT_EXE NAMES clang-format DOC "clang-format executable")
find_program(CLANG_TIDY_EXE NAMES clang-tidy DOC "clang-tidy executable")
find_program(RUN_CLANG_TIDY_EXE NAMES run-clang-tidy run-clang-tidy.py DOC "run-clang-tidy script")

file(GLOB_RECURSE ASYNCWC_ALL_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/tests/*.hpp"
    "${PROJECT_SOURCE_DIR}/tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/examples/*.cpp")

if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXE}" -i ${ASYNCWC_ALL_SOURCES}
        COMMENT "Formatting sources with ${CLANG_FORMAT_EXE}"
        VERBATIM)
    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_EXE}" --dry-run --Werror ${ASYNCWC_ALL_SOURCES}
        COMMENT "Checking formatting with ${CLANG_FORMAT_EXE}"
        VERBATIM)
endif()

# Compile flags understood by GCC but not by clang must not turn into errors.
set(ASYNCWC_CLANG_TIDY_EXTRA_ARGS
    -extra-arg=-Wno-unknown-warning-option
    -extra-arg=-Wno-unused-command-line-argument)

if(CLANG_TIDY_EXE AND RUN_CLANG_TIDY_EXE)
    add_custom_target(tidy
        COMMAND "${RUN_CLANG_TIDY_EXE}"
                -clang-tidy-binary "${CLANG_TIDY_EXE}"
                -p "${PROJECT_BINARY_DIR}"
                -quiet
                ${ASYNCWC_CLANG_TIDY_EXTRA_ARGS}
                "^${PROJECT_SOURCE_DIR}/(src|tests|examples)/"
        COMMENT "Running clang-tidy over src/, tests/ and examples/"
        VERBATIM)
endif()

# Runs clang-tidy as part of compiling `target` when ASYNCWC_ENABLE_CLANG_TIDY is set.
function(asyncwc_enable_clang_tidy target)
    if(NOT ASYNCWC_ENABLE_CLANG_TIDY)
        return()
    endif()
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "ASYNCWC_ENABLE_CLANG_TIDY is ON but clang-tidy was not found")
    endif()
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY
        "${CLANG_TIDY_EXE};--extra-arg=-Wno-unknown-warning-option;--extra-arg=-Wno-unused-command-line-argument")
endfunction()
