#include "mafia/coroutines.hpp"

#include "mafia/game_master.hpp"
#include "mafia/roles/civilian.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using mafia::ExecutionMode;
using mafia::GameMaster;
using mafia::GameResult;
using mafia::GameView;
using mafia::InputBroker;
using mafia::kNoTarget;
using mafia::make_shared_ptr;
using mafia::NightAction;
using mafia::Player;
using mafia::PlayerId;
using mafia::Role;
using mafia::run_all;
using mafia::run_blocking;
using mafia::SharedPtr;
using mafia::Task;
using mafia::roles::Civilian;

namespace {

// ---------- Task ----------

Task<int> returns_value(int value) { co_return value; }

Task<int> adds_one_to_inner(int value) {
    int inner = co_await returns_value(value);  // вложенное ожидание другой корутины
    co_return inner + 1;
}

Task<int> throws_inside() {
    throw std::runtime_error("boom");
    co_return 0;
}

void test_task_is_lazy_until_resumed() {
    bool started = false;
    auto make = [&started]() -> Task<int> {
        started = true;
        co_return 7;
    };
    Task<int> task = make();
    assert(!started);  // корутина не выполняется, пока её не запустят
    assert(!task.done());

    task.resume();
    assert(started);
    assert(task.done());
    assert(task.result() == 7);
}

void test_task_awaiting_nested_task() {
    Task<int> task = adds_one_to_inner(41);
    task.resume();
    assert(task.done());
    assert(task.result() == 42);
}

void test_task_propagates_exception_to_result() {
    Task<int> task = throws_inside();
    task.resume();
    assert(task.done());
    bool caught = false;
    try {
        (void)task.result();
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "boom";
    }
    assert(caught);
}

// ---------- InputBroker + планировщик ----------

Task<int> immediate(std::vector<std::string>& log, int value) {
    log.push_back("bot" + std::to_string(value));
    co_return value;
}

Task<int> asks_human(std::vector<std::string>& log, InputBroker& input) {
    log.push_back("human-asks");
    std::optional<std::string> line = co_await input.read_line();
    log.push_back("human-answered");
    co_return line ? std::stoi(*line) : -1;
}

// Главное свойство корутинного режима: человек, стоящий в очереди ПЕРВЫМ и
// ждущий ввода, не задерживает ботов — они завершаются раньше, чем его
// вопрос вообще получит ответ.
void test_bots_finish_before_waiting_human_is_served() {
    std::istringstream in("99\n");
    std::ostringstream out;
    InputBroker input(in, out);
    std::vector<std::string> log;

    std::vector<Task<int>> tasks;
    tasks.push_back(asks_human(log, input));
    tasks.push_back(immediate(log, 1));
    tasks.push_back(immediate(log, 2));

    std::vector<int> results = run_all(std::move(tasks), input);

    assert((log == std::vector<std::string>{"human-asks", "bot1", "bot2", "human-answered"}));
    assert((results == std::vector<int>{99, 1, 2}));
}

void test_run_all_with_exhausted_input_gives_nullopt() {
    std::istringstream in("");
    std::ostringstream out;
    InputBroker input(in, out);
    std::vector<std::string> log;

    std::vector<Task<int>> tasks;
    tasks.push_back(asks_human(log, input));
    std::vector<int> results = run_all(std::move(tasks), input);
    assert(results.size() == 1 && results[0] == -1);  // EOF не зацикливает и не блокирует
}

void test_run_blocking_drives_single_task() {
    std::istringstream in("5\n");
    std::ostringstream out;
    InputBroker input(in, out);
    std::vector<std::string> log;
    assert(run_blocking(asks_human(log, input), input) == 5);
}

// ---------- GameMaster в режиме корутин ----------

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

std::vector<SharedPtr<Player>> scripted_votes() {
    return {
        make_shared_ptr<ScriptedPlayer>(0, "A", Role::Civilian, 3),
        make_shared_ptr<ScriptedPlayer>(1, "B", Role::Civilian, 3),
        make_shared_ptr<ScriptedPlayer>(2, "C", Role::Civilian, 3),
        make_shared_ptr<ScriptedPlayer>(3, "D", Role::Civilian, 0),
    };
}

void test_coroutine_mode_gives_same_day_result_as_thread_mode() {
    for (ExecutionMode mode : {ExecutionMode::Threads, ExecutionMode::Coroutines}) {
        GameMaster gm(scripted_votes());
        gm.set_execution_mode(mode);
        auto day = gm.play_day();
        assert(day.executed == 3);
        assert(!day.was_tie);
        assert(day.votes.size() == 4);
    }
}

void test_coroutine_mode_human_vote_goes_through_scheduler() {
    // Человек (Civilian 0) голосует за 2, скрипты: 1 -> 2, 2 -> 1. Итог: 2 получает 2 голоса.
    for (ExecutionMode mode : {ExecutionMode::Coroutines, ExecutionMode::Threads}) {
        auto human = make_shared_ptr<Civilian>(0, "Human");
        human->set_interactive();
        std::vector<SharedPtr<Player>> players = {
            human,
            make_shared_ptr<ScriptedPlayer>(1, "B", Role::Civilian, 2),
            make_shared_ptr<ScriptedPlayer>(2, "C", Role::Civilian, 1),
        };
        GameMaster gm(std::move(players));
        gm.set_execution_mode(mode);

        std::istringstream in("2\n");
        std::ostringstream out;
        gm.input().set_streams(in, out);

        auto day = gm.play_day();
        assert(day.executed == 2);
        assert(out.str().find("Дневное голосование") != std::string::npos);
    }
}

void test_coroutine_mode_full_random_games_terminate() {
    for (int trial = 0; trial < 30; ++trial) {
        GameMaster gm(5 + trial % 12, 3);
        gm.set_execution_mode(ExecutionMode::Coroutines);
        int rounds = 0;
        while (gm.check_winner() == GameResult::InProgress) {
            gm.play_round();
            assert(++rounds < 100);  // игра обязана завершаться
        }
    }
}

}  // namespace

int main() {
    test_task_is_lazy_until_resumed();
    test_task_awaiting_nested_task();
    test_task_propagates_exception_to_result();
    test_bots_finish_before_waiting_human_is_served();
    test_run_all_with_exhausted_input_gives_nullopt();
    test_run_blocking_drives_single_task();
    test_coroutine_mode_gives_same_day_result_as_thread_mode();
    test_coroutine_mode_human_vote_goes_through_scheduler();
    test_coroutine_mode_full_random_games_terminate();
    std::cout << "All coroutine tests passed.\n";
    return 0;
}
