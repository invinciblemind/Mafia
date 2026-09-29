add_executable(test_shared_ptr test_shared_ptr.cpp)
target_include_directories(test_shared_ptr PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_shared_ptr PRIVATE Threads::Threads)

add_test(NAME SharedPtrTests COMMAND test_shared_ptr)

add_executable(test_player_roles test_player_roles.cpp)
target_link_libraries(test_player_roles PRIVATE mafia_core)

add_test(NAME PlayerRoleTests COMMAND test_player_roles)
