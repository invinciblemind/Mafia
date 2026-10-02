#include "mafia/roles/mafia_council.hpp"

#include <algorithm>

#include "mafia/game_view.hpp"

namespace mafia::roles {

PlayerId MafiaCouncil::current_boss(const GameView& view) const {
    PlayerId boss = kNoTarget;
    for (const auto& player : view.alive_players) {
        bool is_member = std::find(members.begin(), members.end(), player->id()) != members.end();
        if (is_member && (boss == kNoTarget || player->id() < boss)) {
            boss = player->id();
        }
    }
    return boss;
}

}  // namespace mafia::roles
