#include "mafia/roles/mafia.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Mafia::vote(const GameView& view) {
    // Днём мафия маскируется под мирных, но своих не сдаёт — исключаем всю
    // банду (включая себя) из кандидатов на голос.
    if (is_interactive()) {
        return run_blocking(vote_async(view), *view.input);
    }
    return detail::pick_random_target(view, council_->members);
}

Task<PlayerId> Mafia::vote_async(const GameView& view) {
    if (is_interactive()) {
        co_return co_await detail::prompt_for_target_async(
            *this, view, council_->members, "Дневное голосование: кого подозреваете (вслух, не выдавая банду)?");
    }
    co_return vote(view);
}

NightAction Mafia::act(const GameView& view) {
    if (id_ != council_->current_boss(view)) {
        // Решение единогласно и уже будет озвучено Боссом — остальные
        // члены банды на эту ночь воздерживаются от отдельного хода, даже
        // если это интерактивный игрок: банда уже договорилась в привате.
        return NightAction{ActionType::Kill, kNoTarget};
    }
    if (is_interactive()) {
        return run_blocking(act_async(view), *view.input);
    }
    PlayerId target = detail::pick_random_target(view, council_->members);
    return NightAction{ActionType::Kill, target};
}

Task<NightAction> Mafia::act_async(const GameView& view) {
    if (!is_interactive() || id_ != council_->current_boss(view)) {
        co_return act(view);
    }
    PlayerId target = co_await detail::prompt_for_target_async(*this, view, council_->members,
                                                                 "Вы — Босс банды. Кого убиваете этой ночью?");
    co_return NightAction{ActionType::Kill, target};
}

}  // namespace mafia::roles
