#pragma once

#include <vector>

#include "mafia/player.hpp"

namespace mafia::roles {

// Комиссар — каждую ночь либо проверяет статус игрока, либо стреляет в него
// (но не оба действия за одну ночь). Днём ведёт себя как обычный житель.
//
// Пока не подключён Ведущий, результаты проверок Комиссару не возвращаются —
// поэтому бот-стратегия по умолчанию: всегда проверять ещё не проверенных
// живых игроков и никогда не стрелять "вслепую". Осмысленный выбор между
// Check/Shoot появится, когда Комиссар сможет опираться на историю своих
// прошлых проверок через GameMaster.
class Commissar : public Player {
public:
    using Player::Player;

    Role role() const noexcept override { return Role::Commissar; }

    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;

private:
    std::vector<PlayerId> checked_;
};

}  // namespace mafia::roles
