#include "mafia/roles/civilian.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Civilian::vote(const GameView& view) {
    if (is_interactive()) {
        // Режим нитей: человек отвечает в своей нити и может блокироваться на вводе.
        return run_blocking(vote_async(view), *view.input);
    }
    return detail::pick_random_target(view, {id_});
}

Task<PlayerId> Civilian::vote_async(const GameView& view) {
    if (is_interactive()) {
        co_return co_await detail::prompt_for_target_async(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    co_return vote(view);
}

NightAction Civilian::act(const GameView&) {
    return NightAction{};  // ActionType::None: мирный житель ночью бездействует
}

}  // namespace mafia::roles
