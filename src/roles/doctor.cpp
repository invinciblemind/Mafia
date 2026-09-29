#include "mafia/roles/doctor.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Doctor::vote(const GameView& view) {
    return detail::pick_random_target(view, {id_});
}

NightAction Doctor::act(const GameView& view) {
    PlayerId target = detail::pick_random_target(view, {last_healed_});
    if (target == kNoTarget) {
        // Все живые, кроме прошлой цели, закончились (эндшпиль игры) — но
        // Доктор всё равно не имеет права пропускать ход, поэтому на этот
        // случай снимаем ограничение и лечим кого получится, хоть и повторно.
        target = detail::pick_random_target(view, {});
    }
    last_healed_ = target;
    return NightAction{ActionType::Heal, target};
}

}  // namespace mafia::roles
