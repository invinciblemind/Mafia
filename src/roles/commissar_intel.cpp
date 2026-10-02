#include "mafia/roles/commissar_intel.hpp"

#include <algorithm>

#include "mafia/game_view.hpp"

namespace mafia::roles {

PlayerId CommissarIntel::alive_known_mafia(const GameView& view) const {
    for (const CheckRecord& record : checks) {
        bool alive = std::ranges::any_of(view.alive_players,
                                         [&record](const SharedPtr<Player>& player) { return player->id() == record.target; });
        if (record.is_mafia && alive) {
            return record.target;
        }
    }
    return kNoTarget;
}

}  // namespace mafia::roles
