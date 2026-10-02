#include "mafia/roles/role_utils.hpp"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <optional>
#include <random>
#include <ranges>
#include <string>

namespace mafia::roles::detail {

namespace {

// thread_local, а не общий на всю программу генератор: каждый игрок в итоге
// будет действовать в своей std::thread (п.2 задания), и общий std::mt19937
// без синхронизации при одновременных вызовах из разных потоков — гонка
// данных. thread_local даёт каждому потоку свой экземпляр без блокировок.
std::mt19937& rng() {
    static thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

std::vector<PlayerId> gather_candidates(const GameView& view, const std::vector<PlayerId>& exclude) {
    auto not_excluded = [&exclude](const SharedPtr<Player>& player) {
        return std::ranges::find(exclude, player->id()) == exclude.end();
    };
    std::vector<PlayerId> candidates;
    candidates.reserve(view.alive_players.size());
    std::ranges::copy(view.alive_players | std::views::filter(not_excluded) |
                          std::views::transform([](const SharedPtr<Player>& player) { return player->id(); }),
                      std::back_inserter(candidates));
    return candidates;
}

const SharedPtr<Player>& find_in_view(const GameView& view, PlayerId id) {
    for (const auto& player : view.alive_players) {
        if (player->id() == id) {
            return player;
        }
    }
    for (const auto& player : view.killed_tonight) {
        if (player->id() == id) {
            return player;
        }
    }
    // Не должно происходить: id в candidates всегда взят из этих списков.
    return view.alive_players.front();
}

std::string trim(const std::string& text) {
    const char* spaces = " \t\r\n";
    std::size_t first = text.find_first_not_of(spaces);
    if (first == std::string::npos) {
        return {};
    }
    std::size_t last = text.find_last_not_of(spaces);
    return text.substr(first, last - first + 1);
}

std::optional<long long> parse_integer(const std::string& text) {
    std::string trimmed = trim(text);
    long long value = 0;
    auto [end, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
    if (error != std::errc{} || end != trimmed.data() + trimmed.size()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

PlayerId pick_random_of(const std::vector<PlayerId>& candidates) {
    if (candidates.empty()) {
        return kNoTarget;
    }
    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng())];
}

PlayerId pick_random_target(const GameView& view, const std::vector<PlayerId>& exclude) {
    return pick_random_of(gather_candidates(view, exclude));
}

Task<PlayerId> prompt_for_target_async(const Player& actor, const GameView& view, std::vector<PlayerId> exclude,
                                       std::string prompt, bool allow_skip) {
    co_return co_await prompt_from_candidates_async(actor, view, gather_candidates(view, exclude), std::move(prompt),
                                                    allow_skip);
}

Task<PlayerId> prompt_from_candidates_async(const Player& actor, const GameView& view,
                                            std::vector<PlayerId> candidates, std::string prompt, bool allow_skip) {
    if (candidates.empty()) {
        co_return kNoTarget;
    }

    InputBroker& input = *view.input;
    std::ostream& out = input.out();
    out << "\n[" << actor.name() << "] " << prompt << "\n";
    out << "Доступные игроки:\n";
    for (PlayerId id : candidates) {
        out << "  " << id << ": " << find_in_view(view, id)->name() << "\n";
    }
    if (allow_skip) {
        out << "  -: никого (пропустить ход)\n";
    }

    while (true) {
        out << "Введите id игрока: ";
        out.flush();

        // Приостановка: пока человек "думает", планировщик успевает выполнить ботов.
        std::optional<std::string> line = co_await input.read_line();
        if (!line) {
            // Поток ввода исчерпан: повторное чтение вернуло бы EOF мгновенно, и
            // цикл превратился бы в busy-loop. Выходим со значением по умолчанию.
            if (allow_skip) {
                out << "Ввод недоступен — ход пропущен.\n";
                co_return kNoTarget;
            }
            out << "Ввод недоступен — выбран первый доступный игрок по умолчанию.\n";
            co_return candidates.front();
        }
        if (allow_skip && trim(*line) == "-") {
            co_return kNoTarget;
        }

        std::optional<long long> number = parse_integer(*line);
        if (!number) {
            out << "Некорректный ввод, нужно число.\n";
            continue;
        }
        PlayerId id = static_cast<PlayerId>(*number);
        if (std::find(candidates.begin(), candidates.end(), id) != candidates.end()) {
            co_return id;
        }
        out << "Такого игрока нет среди доступных целей.\n";
    }
}

Task<bool> prompt_yes_no_async(const Player& actor, const GameView& view, std::string question, std::string yes_word,
                               std::string no_word, bool default_on_eof) {
    InputBroker& input = *view.input;
    std::ostream& out = input.out();
    out << "\n[" << actor.name() << "] " << question << " (" << yes_word << "/" << no_word << "): ";
    out.flush();

    while (true) {
        std::optional<std::string> line = co_await input.read_line();
        if (!line) {
            out << "Ввод недоступен — выбран вариант по умолчанию.\n";
            co_return default_on_eof;
        }
        std::string answer = trim(*line);
        if (answer == yes_word) {
            co_return true;
        }
        if (answer == no_word) {
            co_return false;
        }
        out << "Не понял ответ, введите " << yes_word << " или " << no_word << ": ";
        out.flush();
    }
}

}  // namespace mafia::roles::detail
