#include "mafia/roles/mafia.hpp"

#include "mafia/game_view.hpp"
#include "mafia/roles/role_utils.hpp"

namespace mafia::roles {

PlayerId Mafia::vote(const GameView& view) {
    // Днём мафия маскируется под мирных, но своих не сдаёт — исключаем всю
    // банду (включая себя) из кандидатов на голос.
    return detail::pick_random_target(view, council_->members);
}

NightAction Mafia::act(const GameView& view) {
    if (id_ != council_->current_boss(view)) {
        // Решение единогласно и уже будет озвучено Боссом — остальные
        // члены банды на эту ночь воздерживаются от отдельного хода.
        return NightAction{ActionType::Kill, kNoTarget};
    }
    PlayerId target = detail::pick_random_target(view, council_->members);
    return NightAction{ActionType::Kill, target};
}

}  // namespace mafia::roles
