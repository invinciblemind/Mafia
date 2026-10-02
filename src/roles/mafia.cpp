#include "mafia/roles/mafia.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Mafia::vote(const GameView& view) {
    // Днём мафия маскируется под мирных, но своих не сдаёт — исключаем всю
    // банду (включая себя) из кандидатов на голос.
    if (is_interactive()) {
        return detail::prompt_for_target(*this, view, council_->members,
                                          "Дневное голосование: кого подозреваете (вслух, не выдавая банду)?");
    }
    return detail::pick_random_target(view, council_->members);
}

NightAction Mafia::act(const GameView& view) {
    if (id_ != council_->current_boss(view)) {
        // Решение единогласно и уже будет озвучено Боссом — остальные
        // члены банды на эту ночь воздерживаются от отдельного хода, даже
        // если это интерактивный игрок: банда уже договорилась в привате.
        return NightAction{ActionType::Kill, kNoTarget};
    }
    if (is_interactive()) {
        PlayerId target =
            detail::prompt_for_target(*this, view, council_->members, "Вы — Босс банды. Кого убиваете этой ночью?");
        return NightAction{ActionType::Kill, target};
    }
    PlayerId target = detail::pick_random_target(view, council_->members);
    return NightAction{ActionType::Kill, target};
}

}  // namespace mafia::roles
