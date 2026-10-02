#include "mafia/concepts.hpp"

#include "mafia/roles/civilian.hpp"
#include "mafia/roles/commissar.hpp"
#include "mafia/roles/doctor.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/roles/maniac.hpp"

#include <iostream>

using namespace mafia;
using mafia::roles::Civilian;
using mafia::roles::Commissar;
using mafia::roles::Doctor;
using mafia::roles::Mafia;
using mafia::roles::Maniac;

namespace {

struct NotAPlayer {};

// Есть act()/vote()/role(), но не наследник Player — в игроки не годится.
struct DuckTyped {
    NightAction act(const GameView&) { return {}; }
    PlayerId vote(const GameView&) { return 0; }
    Role role() const { return Role::Civilian; }
    bool is_alive() const { return true; }
};

// Корректный игрок с посторонней перегрузкой act(int): она не должна мешать
// проверке настоящего контракта act(const GameView&).
class WithExtraOverload : public Player {
public:
    using Player::Player;
    Role role() const noexcept override { return Role::Civilian; }
    PlayerId vote(const GameView&) override { return 0; }
    NightAction act(const GameView&) override { return {}; }
    int act(int) { return 0; }
};

}  // namespace

// Все пять обязательных ролей — игроки.
static_assert(PlayerLike<Civilian>);
static_assert(PlayerLike<Doctor>);
static_assert(PlayerLike<Commissar>);
static_assert(PlayerLike<Mafia>);
static_assert(PlayerLike<Maniac>);

// Посторонние типы — нет.
static_assert(!PlayerLike<int>);
static_assert(!PlayerLike<NotAPlayer>);
static_assert(!PlayerLike<DuckTyped>);
static_assert(PlayerLike<WithExtraOverload>);

// Лагеря определяются при компиляции по kRole.
static_assert(TownPlayer<Civilian> && TownPlayer<Doctor> && TownPlayer<Commissar>);
static_assert(!TownPlayer<Mafia> && !TownPlayer<Maniac>);
static_assert(MafiaPlayer<Mafia> && !MafiaPlayer<Civilian> && !MafiaPlayer<Maniac>);

// Ночной ход доступен только ролям с ночным действием.
static_assert(NightActivePlayer<Doctor> && NightActivePlayer<Commissar>);
static_assert(NightActivePlayer<Mafia> && NightActivePlayer<Maniac>);
static_assert(!NightActivePlayer<Civilian>);

// requires-выражение вне шаблона не подавляет ошибку подстановки, поэтому
// "можно ли вызвать" оборачиваем в концепт-обёртку.
template <typename T>
concept CanTakeNightTurn = requires(T& player, const GameView& view) { take_night_turn(player, view); };

static_assert(CanTakeNightTurn<Doctor> && CanTakeNightTurn<Commissar>);
static_assert(CanTakeNightTurn<Mafia> && CanTakeNightTurn<Maniac>);
static_assert(!CanTakeNightTurn<Civilian>);

int main() {
    // Среда выполнения: фабрика и ночной ход действительно работают.
    SharedPtr<Player> civilian = make_player<Civilian>(0, "A");
    if (!civilian || civilian->role() != Role::Civilian) {
        return 1;
    }

    Maniac maniac(1, "Maniac");
    GameView view;
    view.alive_players = {make_shared_ptr<Maniac>(1, "Maniac2"), make_shared_ptr<Civilian>(2, "T2")};
    NightAction action = take_night_turn(maniac, view);
    if (action.type != ActionType::Kill) {
        return 1;
    }

    maniac.kill();
    if (take_night_turn(maniac, view).type != ActionType::None) {  // мёртвые не ходят
        return 1;
    }

    std::cout << "All concept tests passed.\n";
    return 0;
}
