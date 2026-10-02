#include "mafia/roles/commissar.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Commissar::vote(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(vote_async(view), *view.input);
    }
    return detail::pick_random_target(view, {id_});
}

Task<PlayerId> Commissar::vote_async(const GameView& view) {
    if (is_interactive()) {
        co_return co_await detail::prompt_for_target_async(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    co_return vote(view);
}

NightAction Commissar::act(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(act_async(view), *view.input);
    }

    std::vector<PlayerId> exclude = checked_;
    exclude.push_back(id_);

    PlayerId target = detail::pick_random_target(view, exclude);
    if (target == kNoTarget) {
        // Все живые уже проверены хотя бы раз — начинаем перепроверять,
        // это лучше, чем пропустить ход (Комиссару запрещено правилами).
        target = detail::pick_random_target(view, {id_});
    }
    if (target != kNoTarget) {
        checked_.push_back(target);
    }
    return NightAction{ActionType::Check, target};
}

Task<NightAction> Commissar::act_async(const GameView& view) {
    if (!is_interactive()) {
        co_return act(view);
    }
    bool wants_to_shoot = co_await detail::prompt_yes_no_async(*this, view, "Ночь: проверить статус или выстрелить?",
                                                                 "shoot", "check", /*default_on_eof=*/false);
    if (wants_to_shoot) {
        PlayerId target = co_await detail::prompt_for_target_async(*this, view, {id_}, "В кого стреляете?");
        co_return NightAction{ActionType::Shoot, target};
    }
    // В отличие от бота, человек волен перепроверять кого угодно — не
    // исключаем checked_, это была лишь эвристика для случайного бота,
    // а не правило игры.
    PlayerId target = co_await detail::prompt_for_target_async(*this, view, {id_}, "Кого проверяете?");
    if (target != kNoTarget) {
        checked_.push_back(target);
    }
    co_return NightAction{ActionType::Check, target};
}

}  // namespace mafia::roles
