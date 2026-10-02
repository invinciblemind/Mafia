#pragma once

#include <vector>

#include "mafia/action.hpp"

namespace mafia {
struct GameView;
}  // namespace mafia

namespace mafia::roles {

struct CheckRecord {
    PlayerId target;
    bool is_mafia;
};

// Общее досье Комиссара и его Сержанта (раздаётся обоим через SharedPtr, как
// MafiaCouncil у банды): кто Комиссар, кто Сержант и какие проверки уже
// состоялись. Результаты проверок записывает Ведущий, когда проверка
// действительно сработала (заблокированная Вором — не записывается).
//
// Досье переживает смену Комиссара: когда Сержант занимает место погибшего
// Комиссара, новый Комиссар получает то же досье со всей историей проверок.
struct CommissarIntel {
    PlayerId commissar_id = kNoTarget;
    PlayerId sergeant_id = kNoTarget;
    std::vector<CheckRecord> checks;

    // Живой игрок, о котором по результатам проверок известно, что он мафия
    // (kNoTarget, если такого нет).
    PlayerId alive_known_mafia(const GameView& view) const;
};

}  // namespace mafia::roles
