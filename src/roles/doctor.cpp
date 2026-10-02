#include "mafia/roles/doctor.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Doctor::vote(const GameView& view) {
    if (is_interactive()) {
        return detail::prompt_for_target(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    return detail::pick_random_target(view, {id_});
}

NightAction Doctor::act(const GameView& view) {
    // Исключение last_healed_ из списка кандидатов одинаково применяется и к
    // боту, и к человеку: правило "нельзя лечить того же два раза подряд"
    // соблюдается автоматически — запрещённой цели просто не будет в списке
    // на выбор, а не потому что игрок сам должен об этом помнить.
    if (is_interactive()) {
        PlayerId target = detail::prompt_for_target(*this, view, {last_healed_},
                                                      "Ночь: кого лечите? (нельзя повторять вчерашний выбор)");
        if (target == kNoTarget) {
            target = detail::prompt_for_target(*this, view, {}, "Больше некого лечить, кроме вчерашней цели:");
        }
        last_healed_ = target;
        return NightAction{ActionType::Heal, target};
    }

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
