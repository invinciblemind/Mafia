#include "mafia/roles/commissar.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Commissar::vote(const GameView& view) {
    return detail::pick_random_target(view, {id_});
}

NightAction Commissar::act(const GameView& view) {
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
