#pragma once

#include <vector>

#include "mafia/player.hpp"
#include "mafia/roles/commissar_intel.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia::roles {

// Комиссар — каждую ночь либо проверяет статус игрока, либо стреляет в него
// (но не оба действия за одну ночь). Днём ведёт себя как обычный житель.
//
// Бот: пока никого не изобличил — проверяет ещё не проверенных; если по
// результатам проверок есть живая известная мафия — стреляет в неё и
// голосует против неё. Вслепую не стреляет.
class Commissar : public Player {
public:
    // Досье общее с Сержантом; без Сержанта у Комиссара просто своё.
    Commissar(PlayerId id, std::string name, SharedPtr<CommissarIntel> intel = make_shared_ptr<CommissarIntel>())
        : Player(id, std::move(name)), intel_(std::move(intel)) {
        // Сержант, ставший Комиссаром, наследует уже проверенных.
        for (const CheckRecord& record : intel_->checks) {
            checked_.push_back(record.target);
        }
    }

    static constexpr Role kRole = Role::Commissar;
    Role role() const noexcept override { return kRole; }

    PlayerId vote(const GameView& view) override;
    NightAction act(const GameView& view) override;

    // Корутинные варианты: у интерактивного игрока приостанавливаются на вводе,
    // у ботов просто возвращают результат синхронных vote()/act().
    Task<PlayerId> vote_async(const GameView& view) override;
    Task<NightAction> act_async(const GameView& view) override;

    const SharedPtr<CommissarIntel>& intel() const noexcept { return intel_; }

    // Вызывается Ведущим, когда проверка сработала: результат попадает в досье,
    // которым пользуются сам Комиссар и его Сержант.
    void record_check(PlayerId target, bool is_mafia) { intel_->checks.push_back({target, is_mafia}); }

private:
    std::vector<PlayerId> checked_;  // решения бота "кого я уже проверял" (эвристика)
    SharedPtr<CommissarIntel> intel_;
};

}  // namespace mafia::roles
