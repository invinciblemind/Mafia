#include "mafia/game_view.hpp"
#include "mafia/roles/civilian.hpp"
#include "mafia/roles/commissar.hpp"
#include "mafia/roles/doctor.hpp"

#include <cassert>
#include <iostream>
#include <set>

using mafia::ActionType;
using mafia::GameView;
using mafia::kNoTarget;
using mafia::PlayerId;
using mafia::Role;
using mafia::Team;
using mafia::make_shared_ptr;
using mafia::roles::Civilian;
using mafia::roles::Commissar;
using mafia::roles::Doctor;

namespace {

void test_basic_identity() {
    Civilian c(0, "Alice");
    assert(c.id() == 0);
    assert(c.name() == "Alice");
    assert(c.is_alive());
    assert(c.role() == Role::Civilian);
    assert(c.team() == Team::Town);
}

void test_kill_marks_dead() {
    Civilian c(1, "Bob");
    assert(c.is_alive());
    c.kill();
    assert(!c.is_alive());
}

void test_act_does_nothing_at_night() {
    Civilian c(2, "Carl");
    GameView view;
    auto action = c.act(view);
    assert(action.type == ActionType::None);
    assert(action.target == kNoTarget);
}

void test_vote_excludes_self_and_picks_among_alive() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    auto b = make_shared_ptr<Civilian>(1, "B");
    auto c = make_shared_ptr<Civilian>(2, "C");

    GameView view;
    view.alive_players = {a, b, c};

    std::set<PlayerId> observed_targets;
    for (int i = 0; i < 200; ++i) {
        PlayerId target = a->vote(view);
        assert(target != a->id());
        assert(target == b->id() || target == c->id());
        observed_targets.insert(target);
    }
    // случайный выбор из двух кандидатов за 200 попыток почти наверняка
    // должен был выбрать обоих хотя бы раз
    assert(observed_targets.size() == 2);
}

void test_vote_with_no_other_alive_players() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    GameView view;
    view.alive_players = {a};
    assert(a->vote(view) == kNoTarget);
}

void test_doctor_identity() {
    Doctor d(0, "Doc");
    assert(d.role() == Role::Doctor);
    assert(d.team() == Team::Town);
}

void test_doctor_never_heals_same_target_twice_in_a_row() {
    auto doc = make_shared_ptr<Doctor>(0, "Doc");
    auto a = make_shared_ptr<Civilian>(1, "A");
    auto b = make_shared_ptr<Civilian>(2, "B");

    GameView view;
    view.alive_players = {doc, a, b};

    PlayerId previous = kNoTarget;
    for (int night = 0; night < 20; ++night) {
        auto action = doc->act(view);
        assert(action.type == ActionType::Heal);
        assert(action.target != previous);  // правило 2: не то же лицо, что вчера
        previous = action.target;
    }
}

void test_doctor_can_heal_self() {
    // Всего два потенциальных пациента (Доктор и A) -> раз нельзя повторяться
    // две ночи подряд, выбор гарантированно чередуется между ними, а значит
    // за несколько ночей Доктор обязательно вылечит и себя тоже.
    auto doc = make_shared_ptr<Doctor>(0, "Doc");
    auto a = make_shared_ptr<Civilian>(1, "A");
    GameView view;
    view.alive_players = {doc, a};

    bool healed_self = false;
    PlayerId previous = kNoTarget;
    for (int night = 0; night < 10; ++night) {
        auto action = doc->act(view);
        assert(action.target != previous);
        if (action.target == doc->id()) {
            healed_self = true;
        }
        previous = action.target;
    }
    assert(healed_self);
}

void test_commissar_identity() {
    Commissar c(0, "Com");
    assert(c.role() == Role::Commissar);
    assert(c.team() == Team::Town);
}

void test_commissar_checks_each_alive_player_before_repeating() {
    auto com = make_shared_ptr<Commissar>(0, "Com");
    auto a = make_shared_ptr<Civilian>(1, "A");
    auto b = make_shared_ptr<Civilian>(2, "B");

    GameView view;
    view.alive_players = {com, a, b};

    std::set<PlayerId> checked_in_first_pass;
    for (int i = 0; i < 2; ++i) {
        auto action = com->act(view);
        assert(action.type == ActionType::Check);
        assert(action.target != com->id());
        checked_in_first_pass.insert(action.target);
    }
    // за первые два хода Комиссар обязан проверить обоих РАЗНЫХ живых
    // игроков, прежде чем повторяться
    assert(checked_in_first_pass.size() == 2);
}

}  // namespace

int main() {
    test_basic_identity();
    test_kill_marks_dead();
    test_act_does_nothing_at_night();
    test_vote_excludes_self_and_picks_among_alive();
    test_vote_with_no_other_alive_players();
    test_doctor_identity();
    test_doctor_never_heals_same_target_twice_in_a_row();
    test_doctor_can_heal_self();
    test_commissar_identity();
    test_commissar_checks_each_alive_player_before_repeating();
    std::cout << "All Player/Role tests passed.\n";
    return 0;
}
