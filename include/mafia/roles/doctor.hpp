#pragma once

#include "mafia/player.hpp"

namespace mafia::roles {

// Доктор — каждую ночь лечит одного игрока (может лечить себя), но не имеет
// права лечить того же игрока две ночи подряд. Днём ведёт себя как обычный
// житель, ничем не выдавая роль.
class Doctor : public Player {
public:
    using Player::Player;

    Role role() const noexcept override { return Role::Doctor; }

    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;

private:
    // Кого лечил прошлой ночью — этого же игрока лечить снова нельзя
    // (правило 2 для Доктора). kNoTarget в первую ночь означает "ограничения
    // ещё нет", и это безопасно: kNoTarget не совпадёт ни с одним id живого
    // игрока, так что исключать в pick_random_target нечего.
    PlayerId last_healed_ = kNoTarget;
};

}  // namespace mafia::roles
