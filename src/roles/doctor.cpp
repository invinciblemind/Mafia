#include "mafia/roles/doctor.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Doctor::vote(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(vote_async(view), *view.input);
    }
    return detail::pick_random_target(view, {id_});
}

Task<PlayerId> Doctor::vote_async(const GameView& view) {
    if (is_interactive()) {
        co_return co_await detail::prompt_for_target_async(*this, view, {id_}, "Дневное голосование: кого подозреваете?");
    }
    co_return vote(view);
}

NightAction Doctor::act(const GameView& view) {
    if (is_interactive()) {
        return run_blocking(act_async(view), *view.input);
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

Task<NightAction> Doctor::act_async(const GameView& view) {
    if (!is_interactive()) {
        co_return act(view);
    }
    // Исключение last_healed_ из списка кандидатов одинаково применяется и к
    // боту, и к человеку: правило "нельзя лечить того же два раза подряд"
    // соблюдается автоматически — запрещённой цели просто нет в списке.
    PlayerId target = co_await detail::prompt_for_target_async(
        *this, view, {last_healed_}, "Ночь: кого лечите? (нельзя повторять вчерашний выбор)");
    if (target == kNoTarget) {
        target = co_await detail::prompt_for_target_async(*this, view, {}, "Больше некого лечить, кроме вчерашней цели:");
    }
    last_healed_ = target;
    co_return NightAction{ActionType::Heal, target};
}

}  // namespace mafia::roles
