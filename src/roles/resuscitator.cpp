#include "mafia/roles/resuscitator.hpp"

#include <algorithm>

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

std::vector<PlayerId> Resuscitator::revivable(const GameView& view) const {
    std::vector<PlayerId> candidates;
    for (const SharedPtr<Player>& dead : view.killed_tonight) {
        if (std::ranges::find(revived_, dead->id()) == revived_.end()) {
            candidates.push_back(dead->id());
        }
    }
    return candidates;
}

PlayerId Resuscitator::vote(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(vote_async(view), *view.input);
    }
    return detail::pick_random_target(view, {id_});
}

Task<PlayerId> Resuscitator::vote_async(const GameView& view) {
    if (is_interactive()) {
        co_return co_await detail::prompt_for_target_async(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    co_return vote(view);
}

NightAction Resuscitator::act(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(act_async(view), *view.input);
    }
    PlayerId target = detail::pick_random_of(revivable(view));
    if (target == kNoTarget) {
        return NightAction{};  // воскрешать некого
    }
    return NightAction{ActionType::Resurrect, target};
}

Task<NightAction> Resuscitator::act_async(const GameView& view) {
    if (!is_interactive()) {
        co_return act(view);
    }
    PlayerId target = co_await detail::prompt_from_candidates_async(
        *this, view, revivable(view), "Ночь: кого воскресить? (погибшие этой ночью, роли неизвестны)", /*allow_skip=*/true);
    if (target == kNoTarget) {
        co_return NightAction{};
    }
    co_return NightAction{ActionType::Resurrect, target};
}

}  // namespace mafia::roles
