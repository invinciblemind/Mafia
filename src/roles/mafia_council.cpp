#include "mafia/roles/mafia_council.hpp"

#include <algorithm>

#include "mafia/game_view.hpp"

namespace mafia::roles {

PlayerId MafiaCouncil::current_boss(const GameView& view) const {
    // Лучший кандидат: сначала обычные мафиози, затем воры; среди равных —
    // наименьший id.
    auto is_in = [](const std::vector<PlayerId>& ids, PlayerId id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    PlayerId boss = kNoTarget;
    bool boss_is_thief = true;
    for (const auto& player : view.alive_players) {
        PlayerId id = player->id();
        if (!is_in(members, id)) {
            continue;
        }
        bool is_thief = is_in(thieves, id);
        bool better = boss == kNoTarget || (boss_is_thief && !is_thief) || (boss_is_thief == is_thief && id < boss);
        if (better) {
            boss = id;
            boss_is_thief = is_thief;
        }
    }
    return boss;
}

}  // namespace mafia::roles
