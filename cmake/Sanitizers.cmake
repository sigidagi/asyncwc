include_guard(GLOBAL)

if(NOT ASYNCWC_ENABLE_SANITIZERS)
    return()
endif()

if(MSVC)
    add_compile_options(/fsanitize=address)
else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()
