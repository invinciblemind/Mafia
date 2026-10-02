#include "mafia/roles/commissar.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Commissar::vote(const GameView& view) {
    if (is_interactive()) {
        return detail::prompt_for_target(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    return detail::pick_random_target(view, {id_});
}

NightAction Commissar::act(const GameView& view) {
    if (is_interactive()) {
        bool wants_to_shoot = detail::prompt_yes_no(*this, "Ночь: проверить статус или выстрелить?", "shoot",
                                                     "check", /*default_on_eof=*/false);
        if (wants_to_shoot) {
            PlayerId target = detail::prompt_for_target(*this, view, {id_}, "В кого стреляете?");
            return NightAction{ActionType::Shoot, target};
        }
        // В отличие от бота, человек волен перепроверять кого угодно — не
        // исключаем checked_, это была лишь эвристика для случайного бота,
        // а не правило игры.
        PlayerId target = detail::prompt_for_target(*this, view, {id_}, "Кого проверяете?");
        if (target != kNoTarget) {
            checked_.push_back(target);
        }
        return NightAction{ActionType::Check, target};
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

}  // namespace mafia::roles
