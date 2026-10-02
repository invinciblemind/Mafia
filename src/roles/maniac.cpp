#include "mafia/roles/maniac.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Maniac::vote(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(vote_async(view), *view.input);
    }
    return detail::pick_random_target(view, {id_});
}

Task<PlayerId> Maniac::vote_async(const GameView& view) {
    if (is_interactive()) {
        co_return co_await detail::prompt_for_target_async(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    co_return vote(view);
}

NightAction Maniac::act(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(act_async(view), *view.input);
    }
    PlayerId target = detail::pick_random_target(view, {id_});
    return NightAction{ActionType::Kill, target};
}

Task<NightAction> Maniac::act_async(const GameView& view) {
    if (!is_interactive()) {
        co_return act(view);
    }
    PlayerId target = co_await detail::prompt_for_target_async(*this, view, {id_}, "Ночь: кого убиваете?");
    co_return NightAction{ActionType::Kill, target};
}

}  // namespace mafia::roles
