cmake_minimum_required(VERSION 3.20)
project(mafia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# Библиотека с игровой логикой (роли, GameView и т.д.), переиспользуемая
# и исполняемым файлом, и тестами — чтобы не собирать одни и те же .cpp
# по два раза и не дублировать список исходников в двух местах.
add_library(mafia_core
    src/game_master.cpp
    src/roles/role_utils.cpp
    src/roles/civilian.cpp
    src/roles/doctor.cpp
    src/roles/commissar.cpp
    src/roles/mafia_council.cpp
    src/roles/mafia.cpp
    src/roles/maniac.cpp
)
target_include_directories(mafia_core PUBLIC include)
target_link_libraries(mafia_core PUBLIC Threads::Threads)

add_executable(mafia src/main.cpp)
target_link_libraries(mafia PRIVATE mafia_core)

enable_testing()
add_subdirectory(tests)
