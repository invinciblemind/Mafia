add_executable(test_shared_ptr test_shared_ptr.cpp)
target_include_directories(test_shared_ptr PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_shared_ptr PRIVATE Threads::Threads)

add_test(NAME SharedPtrTests COMMAND test_shared_ptr)

add_executable(test_player_roles test_player_roles.cpp)
target_link_libraries(test_player_roles PRIVATE mafia_core)

add_test(NAME PlayerRoleTests COMMAND test_player_roles)

add_executable(test_mafia_maniac test_mafia_maniac.cpp)
target_link_libraries(test_mafia_maniac PRIVATE mafia_core)

add_test(NAME MafiaManiacTests COMMAND test_mafia_maniac)

add_executable(test_game_master test_game_master.cpp)
target_link_libraries(test_game_master PRIVATE mafia_core)

add_test(NAME GameMasterTests COMMAND test_game_master)

add_executable(test_interactive test_interactive.cpp)
target_link_libraries(test_interactive PRIVATE mafia_core)

add_test(NAME InteractiveTests COMMAND test_interactive)

add_executable(test_game_logger test_game_logger.cpp)
target_link_libraries(test_game_logger PRIVATE mafia_core)

add_test(NAME GameLoggerTests COMMAND test_game_logger)

add_executable(test_concepts test_concepts.cpp)
target_link_libraries(test_concepts PRIVATE mafia_core)

add_test(NAME ConceptTests COMMAND test_concepts)

add_executable(test_coroutines test_coroutines.cpp)
target_link_libraries(test_coroutines PRIVATE mafia_core)

add_test(NAME CoroutineTests COMMAND test_coroutines)

add_executable(test_extra_roles test_extra_roles.cpp)
target_link_libraries(test_extra_roles PRIVATE mafia_core)

add_test(NAME ExtraRoleTests COMMAND test_extra_roles)

add_executable(test_role_config test_role_config.cpp)
target_link_libraries(test_role_config PRIVATE mafia_core)
target_compile_definitions(test_role_config PRIVATE MAFIA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME RoleConfigTests COMMAND test_role_config)
