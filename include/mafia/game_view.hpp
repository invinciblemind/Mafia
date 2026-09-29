#pragma once

#include <vector>

#include "mafia/player.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

// Read-only снимок игры, который Ведущий передаёт игроку в момент его хода.
// Игрок не имеет прямого доступа к GameMaster или к другим игрокам напрямую —
// он видит только то, что разрешено содержимым GameView, и возвращает своё
// решение через act()/vote(), а не выполняет его сам.
//
// Пока здесь минимум — список живых игроков и номер раунда. Позже, когда
// появятся Ведущий и лог игры, сюда естественно добавится история раундов
// (нужна, например, ИИ-агенту и Комиссару, помнящему свои прошлые проверки).
struct GameView {
    std::vector<SharedPtr<Player>> alive_players;
    int round_number = 0;
};

}  // namespace mafia
