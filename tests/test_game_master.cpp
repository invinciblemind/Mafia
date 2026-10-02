#include "mafia/game_master.hpp"
#include "mafia/game_view.hpp"

#include "mafia/roles/civilian.hpp"
#include "mafia/roles/commissar.hpp"
#include "mafia/roles/doctor.hpp"
#include "mafia/roles/mafia.hpp"
#include "mafia/roles/mafia_council.hpp"
#include "mafia/roles/maniac.hpp"

#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <utility>

using mafia::DayReport;
using mafia::GameMaster;
using mafia::GameResult;
using mafia::GameView;
using mafia::kNoTarget;
using mafia::make_shared_ptr;
using mafia::NightAction;
using mafia::NightReport;
using mafia::Player;
using mafia::PlayerId;
using mafia::Role;
using mafia::RoundReport;
using mafia::SharedPtr;
using mafia::roles::Civilian;
using mafia::roles::Commissar;
using mafia::roles::Doctor;
using mafia::roles::Mafia;
using mafia::roles::MafiaCouncil;
using mafia::roles::Maniac;

namespace {

// Тестовый дублёр: голосует за заранее заданного игрока, ночью бездействует,
// а role() возвращает то, что попросили при создании. Нужен, чтобы
// детерминированно проверять подсчёт голосов и условия победы, не полагаясь
// на случайные решения боевых ролей (как мы уже делали для отдельных ролей).
class ScriptedPlayer : public Player {
public:
    ScriptedPlayer(PlayerId id, std::string name, Role role, PlayerId vote_for)
        : Player(id, std::move(name)), role_(role), vote_for_(vote_for) {}

    Role role() const noexcept override { return role_; }
    PlayerId vote(const GameView&) override { return vote_for_; }
    NightAction act(const GameView&) override { return NightAction{}; }

private:
    Role role_;
    PlayerId vote_for_;
};

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
    gm.play_day();  // round_number() увеличивается здесь: день всегда первый
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

void test_day_vote_majority_execution() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "A", Role::Civilian, 3),
        make_shared_ptr<ScriptedPlayer>(1, "B", Role::Civilian, 3),
        make_shared_ptr<ScriptedPlayer>(2, "C", Role::Civilian, 3),
        make_shared_ptr<ScriptedPlayer>(3, "D", Role::Civilian, 0),
    };
    GameMaster gm(std::move(players));
    DayReport report = gm.play_day();

    assert(report.votes.size() == 4);
    assert(!report.was_tie);
    assert(report.executed == 3);                 // 3 голоса (A,B,C) против 1 (D)
    assert(!gm.all_players()[3]->is_alive());
    assert(gm.all_players()[0]->is_alive());
}

void test_day_vote_tie_executes_one_of_the_tied_candidates() {
    // Двое голосуют друг против друга -> гарантированная ничья 1 к 1.
    for (int trial = 0; trial < 30; ++trial) {
        std::vector<SharedPtr<Player>> players = {
            make_shared_ptr<ScriptedPlayer>(0, "A", Role::Civilian, 1),
            make_shared_ptr<ScriptedPlayer>(1, "B", Role::Civilian, 0),
        };
        GameMaster gm(std::move(players));
        DayReport report = gm.play_day();

        assert(report.was_tie);
        assert(report.executed == 0 || report.executed == 1);
    }
}

void test_day_vote_no_execution_when_everyone_abstains() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "A", Role::Civilian, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(1, "B", Role::Civilian, kNoTarget),
    };
    GameMaster gm(std::move(players));
    DayReport report = gm.play_day();

    assert(report.executed == kNoTarget);
    assert(gm.all_players()[0]->is_alive());
    assert(gm.all_players()[1]->is_alive());
}

void test_check_winner_town_wins_when_mafia_and_maniac_dead() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "M", Role::Mafia, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(1, "Maniac", Role::Maniac, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(2, "C", Role::Civilian, kNoTarget),
    };
    GameMaster gm(std::move(players));
    assert(gm.check_winner() == GameResult::InProgress);

    gm.all_players()[0]->kill();
    gm.all_players()[1]->kill();
    assert(gm.check_winner() == GameResult::TownWins);
}

void test_check_winner_mafia_wins_on_numeric_parity() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "M", Role::Mafia, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(1, "C1", Role::Civilian, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(2, "C2", Role::Civilian, kNoTarget),
    };
    GameMaster gm(std::move(players));
    assert(gm.check_winner() == GameResult::InProgress);  // 1 мафия против 2 мирных

    gm.all_players()[1]->kill();
    assert(gm.check_winner() == GameResult::MafiaWins);  // 1 к 1 — равенство
}

void test_check_winner_maniac_wins_alone_with_one_town_member() {
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "Maniac", Role::Maniac, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(1, "C1", Role::Civilian, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(2, "C2", Role::Civilian, kNoTarget),
    };
    GameMaster gm(std::move(players));
    assert(gm.check_winner() == GameResult::InProgress);

    gm.all_players()[1]->kill();  // мафии в этом составе нет, маньяк остаётся с одним мирным
    assert(gm.check_winner() == GameResult::ManiacWins);
}

void test_check_winner_maniac_wins_completely_alone() {
    // Сценарий: были живы Мафия, Маньяк и мирный. Ночью мафия убивает
    // мирного, а Маньяк в ту же ночь убивает мафию -> остаётся только
    // Маньяк, мирных не осталось вовсе (town_alive == 0, а не 1).
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "Maniac", Role::Maniac, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(1, "M", Role::Mafia, kNoTarget),
        make_shared_ptr<ScriptedPlayer>(2, "C", Role::Civilian, kNoTarget),
    };
    GameMaster gm(std::move(players));
    assert(gm.check_winner() == GameResult::InProgress);

    gm.all_players()[1]->kill();  // мафия погибла от руки Маньяка
    gm.all_players()[2]->kill();  // мирный погиб от руки мафии
    assert(gm.check_winner() == GameResult::ManiacWins);
}

void test_play_round_smoke_full_random_setup() {
    GameMaster gm(8, 3);
    RoundReport report = gm.play_round();
    assert(gm.round_number() == 1);
    assert(report.day.votes.size() == 8);  // все 8 живых проголосовали
}

void test_play_round_skips_night_when_day_execution_ends_game() {
    // Голоса: M->1, C1->2, C2->1 => target1 получает 2 голоса (M,C2), target2 — 1 (C1).
    // Казнят C1(id1). Остаются M(мафия) и C2(мирный) -> 1 к 1 -> победа мафии,
    // и Ночь в этом раунде уже не должна разыгрываться.
    std::vector<SharedPtr<Player>> players = {
        make_shared_ptr<ScriptedPlayer>(0, "M", Role::Mafia, 1),
        make_shared_ptr<ScriptedPlayer>(1, "C1", Role::Civilian, 2),
        make_shared_ptr<ScriptedPlayer>(2, "C2", Role::Civilian, 1),
    };
    GameMaster gm(std::move(players));
    RoundReport report = gm.play_round();

    assert(report.day.executed == 1);
    assert(!report.night_played);
    assert(gm.check_winner() == GameResult::MafiaWins);
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
    test_day_vote_majority_execution();
    test_day_vote_tie_executes_one_of_the_tied_candidates();
    test_day_vote_no_execution_when_everyone_abstains();
    test_check_winner_town_wins_when_mafia_and_maniac_dead();
    test_check_winner_mafia_wins_on_numeric_parity();
    test_check_winner_maniac_wins_alone_with_one_town_member();
    test_check_winner_maniac_wins_completely_alone();
    test_play_round_smoke_full_random_setup();
    test_play_round_skips_night_when_day_execution_ends_game();
    std::cout << "All GameMaster tests passed.\n";
    return 0;
}
