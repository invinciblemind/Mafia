#include "mafia/roles/maniac.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Maniac::vote(const GameView& view) {
    return detail::pick_random_target(view, {id_});
}

NightAction Maniac::act(const GameView& view) {
    PlayerId target = detail::pick_random_target(view, {id_});
    return NightAction{ActionType::Kill, target};
}

}  // namespace mafia::roles
