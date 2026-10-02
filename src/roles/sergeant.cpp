#include "mafia/roles/sergeant.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

// Комиссар (intel_->commissar_id) исключён из целей голосования Сержанта:
// он знает, кто это, и голосовать против него не станет.

PlayerId Sergeant::vote(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(vote_async(view), *view.input);
    }
    PlayerId known_mafia = intel_->alive_known_mafia(view);
    if (known_mafia != kNoTarget) {
        return known_mafia;
    }
    return detail::pick_random_target(view, {id_, intel_->commissar_id});
}

Task<PlayerId> Sergeant::vote_async(const GameView& view) {
    if (is_interactive()) {
        co_return co_await detail::prompt_for_target_async(*this, view, {id_, intel_->commissar_id}, "Дневное голосование: кого подозреваете?");
    }
    co_return vote(view);
}

NightAction Sergeant::act(const GameView&) {
    return NightAction{};  // сам проверять не может
}

}  // namespace mafia::roles
