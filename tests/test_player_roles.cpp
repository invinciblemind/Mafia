#include "mafia/game_view.hpp"
#include "mafia/roles/civilian.hpp"

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

}  // namespace

int main() {
    test_basic_identity();
    test_kill_marks_dead();
    test_act_does_nothing_at_night();
    test_vote_excludes_self_and_picks_among_alive();
    test_vote_with_no_other_alive_players();
    std::cout << "All Player/Role tests passed.\n";
    return 0;
}
