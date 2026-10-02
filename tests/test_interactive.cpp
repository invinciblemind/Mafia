#include "mafia/roles/role_utils.hpp"

#include "mafia/roles/civilian.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using mafia::GameView;
using mafia::kNoTarget;
using mafia::make_shared_ptr;
using mafia::PlayerId;
using mafia::roles::Civilian;
using mafia::roles::detail::prompt_for_target;

namespace {

void test_prompt_returns_valid_immediate_choice() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    auto b = make_shared_ptr<Civilian>(1, "B");
    auto c = make_shared_ptr<Civilian>(2, "C");
    GameView view;
    view.alive_players = {a, b, c};

    std::istringstream in("1\n");
    std::ostringstream out;

    PlayerId result = prompt_for_target(*a, view, {a->id()}, "Кого выбираете?", in, out);
    assert(result == 1);
    assert(out.str().find("Кого выбираете?") != std::string::npos);
    assert(out.str().find("A") != std::string::npos);  // имя спрашиваемого в заголовке подсказки
}

void test_prompt_reprompts_on_invalid_then_accepts_valid() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    auto b = make_shared_ptr<Civilian>(1, "B");
    GameView view;
    view.alive_players = {a, b};

    // 5 — валидное число, но не входит в список кандидатов; "oops" — не
    // число вообще; 1 — валидный ответ.
    std::istringstream in("5\noops\n1\n");
    std::ostringstream out;

    PlayerId result = prompt_for_target(*a, view, {a->id()}, "Кого выбираете?", in, out);
    assert(result == 1);

    std::string text = out.str();
    assert(text.find("Такого игрока нет среди доступных целей") != std::string::npos);  // реакция на "5"
    assert(text.find("Некорректный ввод, нужно число") != std::string::npos);            // реакция на "oops"
}

void test_prompt_returns_no_target_without_touching_stream_when_no_candidates() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    GameView view;
    view.alive_players = {a};  // единственный живой — сам опрашиваемый, кандидатов нет

    std::istringstream in("");  // пустой поток: если бы prompt попытался читать, результат был бы мусором
    std::ostringstream out;

    PlayerId result = prompt_for_target(*a, view, {a->id()}, "Кого выбираете?", in, out);
    assert(result == kNoTarget);
    assert(out.str().empty());  // консоль вообще не трогали
}

// Регрессия на реальный баг: если поток ввода исчерпан (EOF) ДО того, как
// получен валидный ответ, цикл не должен уходить в вечный busy-loop — он
// обязан вернуть первого доступного кандидата и завершиться. Раньше здесь
// зависало (обнаружено прогоном настоящего бинарника с нереалистичным
// скриптовым вводом, который никогда не присылал валидный id).
void test_prompt_for_target_returns_default_on_exhausted_stream_instead_of_hanging() {
    auto a = make_shared_ptr<Civilian>(0, "A");
    auto b = make_shared_ptr<Civilian>(1, "B");
    auto c = make_shared_ptr<Civilian>(2, "C");
    GameView view;
    view.alive_players = {a, b, c};

    std::istringstream in("99\noops\n");  // ни одна строка не валидна, затем поток кончается
    std::ostringstream out;

    PlayerId result = prompt_for_target(*a, view, {a->id()}, "Кого выбираете?", in, out);
    assert(result == 1);  // первый кандидат после исключения self(0) -> id 1
    assert(out.str().find("Ввод недоступен") != std::string::npos);
}

void test_prompt_yes_no_returns_valid_immediate_choice() {
    auto a = make_shared_ptr<Civilian>(0, "A");

    std::istringstream in("shoot\n");
    std::ostringstream out;

    bool result = mafia::roles::detail::prompt_yes_no(*a, "Вопрос?", "shoot", "check",
                                                        /*default_on_eof=*/false, in, out);
    assert(result == true);
}

// Та же регрессия, что и выше, но для второй функции с идентичным циклом
// опроса (ask_wants_to_shoot у Комиссара в реальности зависал именно так).
void test_prompt_yes_no_returns_default_on_exhausted_stream_instead_of_hanging() {
    auto a = make_shared_ptr<Civilian>(0, "A");

    std::istringstream in("0\n1\n2\n");  // ни "shoot", ни "check" — только мусор, затем EOF
    std::ostringstream out;

    bool result = mafia::roles::detail::prompt_yes_no(*a, "Вопрос?", "shoot", "check",
                                                        /*default_on_eof=*/false, in, out);
    assert(result == false);  // вернул default_on_eof, не зациклился
    assert(out.str().find("Ввод недоступен") != std::string::npos);
}

}  // namespace

int main() {
    test_prompt_returns_valid_immediate_choice();
    test_prompt_reprompts_on_invalid_then_accepts_valid();
    test_prompt_returns_no_target_without_touching_stream_when_no_candidates();
    test_prompt_for_target_returns_default_on_exhausted_stream_instead_of_hanging();
    test_prompt_yes_no_returns_valid_immediate_choice();
    test_prompt_yes_no_returns_default_on_exhausted_stream_instead_of_hanging();
    std::cout << "All interactive-mode tests passed.\n";
    return 0;
}
