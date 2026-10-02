#pragma once

#include <string>
#include <utility>

#include "mafia/player.hpp"
#include "mafia/roles/commissar_intel.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia::roles {

// Сержант — помощник Комиссара. Знает, кто Комиссар (и Комиссар знает его), и
// видит результаты проверок Комиссара — всё это лежит в общем досье
// CommissarIntel. Сам проверять не может, ночью бездействует. Если Комиссар
// гибнет, Ведущий заменяет Сержанта новым объектом Commissar с тем же досье
// (см. GameMaster): сам объект Sergeant роль не меняет.
class Sergeant : public Player {
public:
    Sergeant(PlayerId id, std::string name, SharedPtr<CommissarIntel> intel)
        : Player(id, std::move(name)), intel_(std::move(intel)) {}

    static constexpr Role kRole = Role::Sergeant;
    Role role() const noexcept override { return kRole; }

    // Бот голосует против известной по проверкам Комиссара живой мафии.
    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;
    Task<PlayerId> vote_async(const GameView& view) override;

    const SharedPtr<CommissarIntel>& intel() const noexcept { return intel_; }

private:
    SharedPtr<CommissarIntel> intel_;
};

}  // namespace mafia::roles
