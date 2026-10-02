#include "mafia/game_master.hpp"

#include "mafia/roles/civilian.hpp"
#include "mafia/roles/commissar.hpp"
#include "mafia/roles/doctor.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/roles/mafia_council.hpp"
#include "mafia/roles/maniac.hpp"

#include <cassert>
#include <iostream>
#include <set>

using mafia::GameMaster;
using mafia::kNoTarget;
using mafia::make_shared_ptr;
using mafia::NightReport;
using mafia::Player;
using mafia::PlayerId;
using mafia::Role;
using mafia::SharedPtr;
using mafia::roles::Civilian;
using mafia::roles::Commissar;
using mafia::roles::Doctor;
using mafia::roles::Mafia;
using mafia::roles::MafiaCouncil;
using mafia::roles::Maniac;

namespace {

void test_role_distribution_counts() {
    GameMaster gm(10, 3);  // floor(10/3) = 3 мафии
    assert(gm.all_players().size() == 10);

    int mafia = 0, doctors = 0, commissars = 0, maniacs = 0, civilians = 0;
    std::set<PlayerId> seen_ids;
    for (const auto& p : gm.all_players()) {
        seen_ids.insert(p->id());
        switch (p->role()) {
            case Role::Mafia: ++mafia; break;
            case Role::Doctor: ++doctors; break;
            case Role::Commissar: ++commissars; break;
            case Role::Maniac: ++maniacs; break;
            case Role::Civilian: ++civilians; break;
        }
    }
    assert(mafia == 3);
    assert(doctors == 1);
    assert(commissars == 1);
    assert(maniacs == 1);
    assert(civilians == 4);
    assert(seen_ids.size() == 10);  // все id различны

    for (PlayerId id = 0; id < 10; ++id) {
        assert(gm.all_players()[id]->id() == id);  // индекс вектора == id
    }
}

void test_mafia_count_floor_with_minimum_one() {
    GameMaster gm(5, 10);  // floor(5/10) = 0 -> должно стать 1
    int mafia = 0;
    for (const auto& p : gm.all_players()) {
        if (p->role() == Role::Mafia) ++mafia;
    }
    assert(mafia == 1);
}

void test_play_night_smoke_full_random_setup() {
    GameMaster gm(8, 3);
    NightReport report = gm.play_night();
    assert(gm.round_number() == 1);
    for (PlayerId id : report.killed) {
        assert(id < 8);
        assert(!gm.all_players()[id]->is_alive());  // отмечен мёртвым у Ведущего
    }
}

void test_mafia_never_targets_own_team_in_full_game() {
    for (int trial = 0; trial < 20; ++trial) {
        GameMaster gm(9, 3);
        NightReport report = gm.play_night();
        if (report.mafia_target != kNoTarget) {
            assert(gm.all_players()[report.mafia_target]->role() != Role::Mafia);
        }
    }
}

void test_heal_blocks_matching_kill_and_only_matching_kill() {
    // Doc(0), Mafia-босс(1, вне банды некого больше исключать), Civilian(2).
    // Мафия обязана выбрать цель среди {0,2}; Доктор выбирает среди {0,1,2}.
    // Инвариант: убийство засчитывается ровно тогда, когда Доктор вылечил
    // НЕ того, кого выбрала мафия.
    for (int trial = 0; trial < 60; ++trial) {
        auto council = make_shared_ptr<MafiaCouncil>();
        council->members = {1};
        std::vector<SharedPtr<Player>> players = {
            make_shared_ptr<Doctor>(0, "Doc"),
            make_shared_ptr<Mafia>(1, "Boss", council),
            make_shared_ptr<Civilian>(2, "C"),
        };
        GameMaster gm(std::move(players));
        NightReport report = gm.play_night();

        assert(report.mafia_target == 0 || report.mafia_target == 2);
        if (report.healed == report.mafia_target) {
            assert(report.killed.empty());
        } else {
            assert(report.killed.size() == 1);
            assert(report.killed[0] == report.mafia_target);
        }
    }
}

void test_commissar_check_reports_maniac_as_not_mafia() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Commissar>(0, "Com"),
        make_shared_ptr<Maniac>(1, "Maniac"),
    };
    GameMaster gm(std::move(players));
    NightReport report = gm.play_night();

    assert(report.commissar_check_target == 1);  // единственный кандидат, кроме себя
    assert(report.commissar_check_result_mafia == false);
}

void test_commissar_check_reports_mafia_as_mafia() {
    auto council = make_shared_ptr<MafiaCouncil>();
    council->members = {1};
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<Commissar>(0, "Com"),
        make_shared_ptr<Mafia>(1, "Boss", council),
    };
    GameMaster gm(std::move(players));
    NightReport report = gm.play_night();

    assert(report.commissar_check_target == 1);
    assert(report.commissar_check_result_mafia == true);
}

}  // namespace

int main() {
    test_role_distribution_counts();
    test_mafia_count_floor_with_minimum_one();
    test_play_night_smoke_full_random_setup();
    test_mafia_never_targets_own_team_in_full_game();
    test_heal_blocks_matching_kill_and_only_matching_kill();
    test_commissar_check_reports_maniac_as_not_mafia();
    test_commissar_check_reports_mafia_as_mafia();
    std::cout << "All GameMaster tests passed.\n";
    return 0;
}
