#pragma once

#include <string>
#include <utility>

#include "mafia/player.hpp"
#include "mafia/roles/mafia_council.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia::roles {

// Мафия — играет группой. Ночное убийство совершается единогласным решением
// банды, которое озвучивается через Босса (см. MafiaCouncil::current_boss).
// Единогласность мы моделируем тем, что рядовые мафиози на ночной ход
// "воздерживаются" (возвращают kNoTarget) — решение и так одно на всех,
// у нас просто нет настоящих переговоров между ботами (пока не подключены
// интерактивный режим/ИИ-агент).
class Mafia : public Player {
public:
    Mafia(PlayerId id, std::string name, SharedPtr<MafiaCouncil> council)
        : Player(id, std::move(name)), council_(std::move(council)) {}

    static constexpr Role kRole = Role::Mafia;
    Role role() const noexcept override { return kRole; }

    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;

    // Общий стол банды: через него Ведущий (main) узнаёт состав команды и
    // текущего Босса, чтобы рассказать об этом игроку-человеку.
    const SharedPtr<MafiaCouncil>& council() const noexcept { return council_; }

private:
    SharedPtr<MafiaCouncil> council_;
};

}  // namespace mafia::roles
