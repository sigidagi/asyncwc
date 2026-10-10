foreach(example simple_client reconnecting_client)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE asyncwc::asyncwc)
    asyncwc_set_warnings(${example})
    asyncwc_enable_clang_tidy(${example})
endforeach()

# A tiny local server to try the clients against: ./echo_server 8080
add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE Boost::headers Threads::Threads)
target_compile_features(echo_server PRIVATE cxx_std_20)
asyncwc_set_warnings(echo_server)
asyncwc_enable_clang_tidy(echo_server)
