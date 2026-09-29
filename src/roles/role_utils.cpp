#include "mafia/roles/role_utils.hpp"

#include <algorithm>
#include <random>

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

}  // namespace

PlayerId pick_random_target(const GameView& view, const std::vector<PlayerId>& exclude) {
    std::vector<PlayerId> candidates;
    candidates.reserve(view.alive_players.size());
    for (const auto& player : view.alive_players) {
        bool is_excluded = std::find(exclude.begin(), exclude.end(), player->id()) != exclude.end();
        if (!is_excluded) {
            candidates.push_back(player->id());
        }
    }
    if (candidates.empty()) {
        return kNoTarget;
    }
    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng())];
}

}  // namespace mafia::roles::detail
