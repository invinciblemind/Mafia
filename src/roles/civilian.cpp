#include "mafia/roles/civilian.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Civilian::vote(const GameView& view) {
    if (is_interactive()) {
        return detail::prompt_for_target(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    return detail::pick_random_target(view, {id_});
}

NightAction Civilian::act(const GameView&) {
    return NightAction{};  // ActionType::None: мирный житель ночью бездействует
}

}  // namespace mafia::roles
