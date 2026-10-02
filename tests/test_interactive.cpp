#include "mafia/roles/role_utils.hpp"

#include "mafia/roles/civilian.hpp"
#include "mafia/roles/doctor.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using mafia::GameView;
using mafia::InputBroker;
using mafia::kNoTarget;
using mafia::make_shared_ptr;
using mafia::PlayerId;
using mafia::run_blocking;
using mafia::roles::Civilian;
using mafia::roles::Doctor;
using mafia::roles::detail::prompt_for_target_async;
using mafia::roles::detail::prompt_yes_no_async;

namespace {

// Одна «консоль» на тест: потоки подставные, поэтому тест никогда не
// блокируется на настоящей клавиатуре.
struct FakeConsole {
    std::istringstream in;
    std::ostringstream out;
    InputBroker broker;

    explicit FakeConsole(const std::string& typed) : in(typed), broker(in, out) {}
};

PlayerId ask_target(FakeConsole& console, const mafia::Player& actor, GameView& view,
                    std::vector<PlayerId> exclude, std::string prompt) {
    view.input = &console.broker;
    return run_blocking(prompt_for_target_async(actor, view, std::move(exclude), std::move(prompt)), console.broker);
}

void test_prompt_returns_valid_immediate_choice() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    auto b = make_shared_ptr<Civilian>(1, "B");
    auto c = make_shared_ptr<Civilian>(2, "C");
    GameView view;
    view.alive_players = {a, b, c};

    FakeConsole console("1\n");
    PlayerId result = ask_target(console, *a, view, {a->id()}, "Кого выбираете?");
    assert(result == 1);
    assert(console.out.str().find("Кого выбираете?") != std::string::npos);
    assert(console.out.str().find("A") != std::string::npos);  // имя спрашиваемого в заголовке подсказки
}

void test_prompt_reprompts_on_invalid_then_accepts_valid() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    auto b = make_shared_ptr<Civilian>(1, "B");
    GameView view;
    view.alive_players = {a, b};

    // 5 — валидное число, но не входит в список кандидатов; "oops" — не
    // число вообще; 1 — валидный ответ.
    FakeConsole console("5\noops\n1\n");
    PlayerId result = ask_target(console, *a, view, {a->id()}, "Кого выбираете?");
    assert(result == 1);

    std::string text = console.out.str();
    assert(text.find("Такого игрока нет среди доступных целей") != std::string::npos);  // реакция на "5"
    assert(text.find("Некорректный ввод, нужно число") != std::string::npos);            // реакция на "oops"
}

void test_prompt_returns_no_target_without_touching_console_when_no_candidates() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    GameView view;
    view.alive_players = {a};  // единственный живой — сам опрашиваемый, кандидатов нет

    FakeConsole console("");
    PlayerId result = ask_target(console, *a, view, {a->id()}, "Кого выбираете?");
    assert(result == kNoTarget);
    assert(console.out.str().empty());  // консоль вообще не трогали
}

// Регрессия на реальный баг: если ввод исчерпан (EOF) раньше, чем получен
// валидный ответ, ожидание не должно превращаться в вечный цикл — обязан
// вернуться первый кандидат.
void test_prompt_for_target_returns_default_on_exhausted_input_instead_of_hanging() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    auto b = make_shared_ptr<Civilian>(1, "B");
    auto c = make_shared_ptr<Civilian>(2, "C");
    GameView view;
    view.alive_players = {a, b, c};

    FakeConsole console("99\noops\n");  // ни одна строка не валидна, затем ввод кончается
    PlayerId result = ask_target(console, *a, view, {a->id()}, "Кого выбираете?");
    assert(result == 1);  // первый кандидат после исключения self(0)
    assert(console.out.str().find("Ввод недоступен") != std::string::npos);
}

bool ask_yes_no(FakeConsole& console, const mafia::Player& actor, GameView& view) {
    view.input = &console.broker;
    return run_blocking(prompt_yes_no_async(actor, view, "Вопрос?", "shoot", "check", /*default_on_eof=*/false),
                        console.broker);
}

void test_prompt_yes_no_returns_valid_immediate_choice() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    GameView view;
    FakeConsole console("shoot\n");
    assert(ask_yes_no(console, *a, view) == true);
}

void test_prompt_yes_no_returns_default_on_exhausted_input_instead_of_hanging() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    GameView view;
    FakeConsole console("0\n1\n2\n");  // ни "shoot", ни "check" — только мусор, затем EOF
    assert(ask_yes_no(console, *a, view) == false);
    assert(console.out.str().find("Ввод недоступен") != std::string::npos);
}

// Роль целиком: интерактивный Мирный житель в режиме нитей (синхронный
// vote()) берёт решение у человека через тот же корутинный путь.
void test_interactive_civilian_sync_vote_reads_from_console() {
    auto human = make_shared_ptr<Civilian>(0, "Human");
    auto other = make_shared_ptr<Civilian>(1, "Other");
    human->set_interactive();

    GameView view;
    view.alive_players = {human, other};
    FakeConsole console("1\n");
    view.input = &console.broker;

    assert(human->vote(view) == 1);
}

// Правило Доктора соблюдается и для человека: вчерашняя цель не предлагается.
void test_interactive_doctor_cannot_pick_yesterdays_target() {
    auto doctor = make_shared_ptr<Doctor>(0, "Doc");
    auto a = make_shared_ptr<Civilian>(1, "A");
    auto b = make_shared_ptr<Civilian>(2, "B");
    doctor->set_interactive();

    GameView view;
    view.alive_players = {doctor, a, b};
    FakeConsole console("1\n"      // первая ночь: лечит A(1)
                        "1\n2\n");  // вторая ночь: "1" не предложено -> отклонено, затем "2"
    view.input = &console.broker;

    assert(doctor->act(view).target == 1);
    assert(doctor->act(view).target == 2);
    assert(console.out.str().find("Такого игрока нет среди доступных целей") != std::string::npos);
}

}  // namespace

int main() {
    test_prompt_returns_valid_immediate_choice();
    test_prompt_reprompts_on_invalid_then_accepts_valid();
    test_prompt_returns_no_target_without_touching_console_when_no_candidates();
    test_prompt_for_target_returns_default_on_exhausted_input_instead_of_hanging();
    test_prompt_yes_no_returns_valid_immediate_choice();
    test_prompt_yes_no_returns_default_on_exhausted_input_instead_of_hanging();
    test_interactive_civilian_sync_vote_reads_from_console();
    test_interactive_doctor_cannot_pick_yesterdays_target();
    std::cout << "All interactive-mode tests passed.\n";
    return 0;
}
