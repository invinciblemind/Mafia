#include "mafia/roles/role_utils.hpp"

#include <algorithm>
#include <limits>
#include <random>
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
    std::vector<PlayerId> candidates;
    candidates.reserve(view.alive_players.size());
    for (const auto& player : view.alive_players) {
        bool is_excluded = std::find(exclude.begin(), exclude.end(), player->id()) != exclude.end();
        if (!is_excluded) {
            candidates.push_back(player->id());
        }
    }
    return candidates;
}

const SharedPtr<Player>& find_in_view(const GameView& view, PlayerId id) {
    for (const auto& player : view.alive_players) {
        if (player->id() == id) {
            return player;
        }
    }
    // Не должно происходить: id в candidates всегда взят из view.alive_players.
    return view.alive_players.front();
}

}  // namespace

PlayerId pick_random_target(const GameView& view, const std::vector<PlayerId>& exclude) {
    std::vector<PlayerId> candidates = gather_candidates(view, exclude);
    if (candidates.empty()) {
        return kNoTarget;
    }
    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng())];
}

PlayerId prompt_for_target(const Player& actor, const GameView& view, const std::vector<PlayerId>& exclude,
                           std::string_view prompt, std::istream& in, std::ostream& out) {
    std::vector<PlayerId> candidates = gather_candidates(view, exclude);
    if (candidates.empty()) {
        return kNoTarget;
    }

    out << "\n[" << actor.name() << "] " << prompt << "\n";
    out << "Доступные игроки:\n";
    for (PlayerId id : candidates) {
        out << "  " << id << ": " << find_in_view(view, id)->name() << "\n";
    }

    while (true) {
        out << "Введите id игрока: ";
        out.flush();

        long long choice;
        if (in >> choice) {
            PlayerId id = static_cast<PlayerId>(choice);
            if (std::find(candidates.begin(), candidates.end(), id) != candidates.end()) {
                return id;
            }
            out << "Такого игрока нет среди доступных целей.\n";
            continue;
        }

        // Чтение не удалось. Если поток ввода исчерпан (EOF) — это НЕ повод
        // переспрашивать снова: после eof() следующий in >> choice провалится
        // мгновенно и без ожидания, так что while(true) превратился бы в
        // бесконечный busy-loop, а не в ожидание реального ввода. Поэтому при
        // исчерпанном потоке берём первого доступного кандидата и выходим.
        if (in.eof()) {
            out << "Ввод недоступен — выбран первый доступный игрок по умолчанию.\n";
            return candidates.front();
        }
        in.clear();
        in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        out << "Некорректный ввод, нужно число.\n";
    }
}

bool prompt_yes_no(const Player& actor, std::string_view question, std::string_view yes_word,
                    std::string_view no_word, bool default_on_eof, std::istream& in, std::ostream& out) {
    out << "\n[" << actor.name() << "] " << question << " (" << yes_word << "/" << no_word << "): ";
    out.flush();

    std::string answer;
    while (true) {
        if (!(in >> answer)) {
            // Та же защита от busy-loop на исчерпанном потоке, что и в
            // prompt_for_target выше.
            out << "Ввод недоступен — выбран вариант по умолчанию.\n";
            return default_on_eof;
        }
        if (answer == yes_word) {
            return true;
        }
        if (answer == no_word) {
            return false;
        }
        out << "Не понял ответ, введите " << yes_word << " или " << no_word << ": ";
    }
}

}  // namespace mafia::roles::detail
