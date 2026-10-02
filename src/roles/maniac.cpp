#include "mafia/roles/maniac.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Maniac::vote(const GameView& view) {
    if (is_interactive()) {
        return detail::prompt_for_target(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    return detail::pick_random_target(view, {id_});
}

NightAction Maniac::act(const GameView& view) {
    if (is_interactive()) {
        PlayerId target = detail::prompt_for_target(*this, view, {id_}, "Ночь: кого убиваете?");
        return NightAction{ActionType::Kill, target};
    }
    PlayerId target = detail::pick_random_target(view, {id_});
    return NightAction{ActionType::Kill, target};
}

}  // namespace mafia::roles
