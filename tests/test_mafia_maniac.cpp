#include "mafia/game_view.hpp"
#include "mafia/roles/civilian.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/roles/mafia_council.hpp"
#include "mafia/roles/maniac.hpp"

#include <cassert>
#include <iostream>

using mafia::ActionType;
using mafia::GameView;
using mafia::kNoTarget;
using mafia::PlayerId;
using mafia::Role;
using mafia::Team;
using mafia::make_shared_ptr;
using mafia::roles::Civilian;
using mafia::roles::Mafia;
using mafia::roles::MafiaCouncil;
using mafia::roles::Maniac;

namespace {

void test_mafia_council_current_boss_picks_smallest_alive_id() {
    auto council = make_shared_ptr<MafiaCouncil>();
    council->members = {2, 5, 7};

    auto m2 = make_shared_ptr<Mafia>(2, "M2", council);
    auto m5 = make_shared_ptr<Mafia>(5, "M5", council);
    auto m7 = make_shared_ptr<Mafia>(7, "M7", council);

    GameView view;
    view.alive_players = {m2, m5, m7};
    assert(council->current_boss(view) == 2);

    view.alive_players = {m5, m7};  // Босс (2) погиб
    assert(council->current_boss(view) == 5);

    view.alive_players = {};
    assert(council->current_boss(view) == kNoTarget);
}

void test_only_boss_decides_kill_others_abstain() {
    auto council = make_shared_ptr<MafiaCouncil>();
    council->members = {0, 1};

    auto boss = make_shared_ptr<Mafia>(0, "Boss", council);
    auto grunt = make_shared_ptr<Mafia>(1, "Grunt", council);
    auto victim = make_shared_ptr<Civilian>(2, "Victim");

    GameView view;
    view.alive_players = {boss, grunt, victim};

    auto boss_action = boss->act(view);
    assert(boss_action.type == ActionType::Kill);
    assert(boss_action.target == victim->id());  // единственный не-мафиози кандидат

    auto grunt_action = grunt->act(view);
    assert(grunt_action.type == ActionType::Kill);
    assert(grunt_action.target == kNoTarget);  // воздержался: решение уже принял Босс
}

void test_boss_succession_after_boss_dies() {
    auto council = make_shared_ptr<MafiaCouncil>();
    council->members = {0, 1};

    auto grunt = make_shared_ptr<Mafia>(1, "Grunt", council);
    auto victim = make_shared_ptr<Civilian>(2, "Victim");

    GameView view;
    view.alive_players = {grunt, victim};  // Босс (id 0) мёртв, его нет среди живых

    auto grunt_action = grunt->act(view);
    assert(grunt_action.type == ActionType::Kill);
    assert(grunt_action.target == victim->id());  // теперь Grunt — Босс банды
}

void test_mafia_never_targets_teammates() {
    auto council = make_shared_ptr<MafiaCouncil>();
    council->members = {0, 1};

    auto boss = make_shared_ptr<Mafia>(0, "Boss", council);
    auto grunt = make_shared_ptr<Mafia>(1, "Grunt", council);
    auto a = make_shared_ptr<Civilian>(2, "A");
    auto b = make_shared_ptr<Civilian>(3, "B");

    GameView view;
    view.alive_players = {boss, grunt, a, b};

    for (int i = 0; i < 50; ++i) {
        PlayerId day_vote = boss->vote(view);
        assert(day_vote != boss->id());
        assert(day_vote != grunt->id());

        auto night_action = boss->act(view);
        assert(night_action.target != boss->id());
        assert(night_action.target != grunt->id());
    }
}

void test_mafia_identity() {
    auto council = make_shared_ptr<MafiaCouncil>();
    council->members = {0};
    Mafia m(0, "M", council);
    assert(m.role() == Role::Mafia);
    assert(m.team() == Team::Mafia);
}

void test_maniac_identity() {
    Maniac m(0, "Maniac");
    assert(m.role() == Role::Maniac);
    assert(m.team() == Team::Independent);
}

void test_maniac_always_kills_someone_alive_not_self() {
    auto m = make_shared_ptr<Maniac>(0, "Maniac");
    auto a = make_shared_ptr<Civilian>(1, "A");
    auto b = make_shared_ptr<Civilian>(2, "B");
    GameView view;
    view.alive_players = {m, a, b};

    for (int i = 0; i < 50; ++i) {
        auto action = m->act(view);
        assert(action.type == ActionType::Kill);
        assert(action.target != m->id());
        assert(action.target == a->id() || action.target == b->id());
    }
}

}  // namespace

int main() {
    test_mafia_council_current_boss_picks_smallest_alive_id();
    test_only_boss_decides_kill_others_abstain();
    test_boss_succession_after_boss_dies();
    test_mafia_never_targets_teammates();
    test_mafia_identity();
    test_maniac_identity();
    test_maniac_always_kills_someone_alive_not_self();
    std::cout << "All Mafia/Maniac tests passed.\n";
    return 0;
}
