#include "mafia/roles/thief.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

NightAction Thief::act(const GameView& view) {
    if (id_ == council()->current_boss(view)) {
        return Mafia::act(view);  // последний в банде: стреляет как обычный мафиози
    }
    if (is_interactive()) {
        return run_blocking(act_async(view), *view.input);
    }
    // Свои (вся банда, включая себя) блокировать бессмысленно.
    PlayerId target = detail::pick_random_target(view, council()->members);
    return NightAction{ActionType::Block, target};
}

Task<NightAction> Thief::act_async(const GameView& view) {
    if (!is_interactive()) {
        co_return act(view);
    }
    if (id_ == council()->current_boss(view)) {
        co_return co_await Mafia::act_async(view);
    }
    PlayerId target = co_await detail::prompt_for_target_async(
        *this, view, council()->members, "Ночь: чьи способности блокируете? (на Мирного жителя блокировка не действует)");
    co_return NightAction{ActionType::Block, target};
}

}  // namespace mafia::roles
