#include "mafia/roles/civilian.hpp"

#include <random>
#include <vector>

#include "mafia/game_view.hpp"

namespace mafia::roles {

namespace {

// thread_local, а не общий на всю программу генератор: каждый игрок в итоге
// будет действовать в своей std::thread (п.2 задания), и общий
// std::mt19937 без синхронизации при одновременных вызовах из разных
// потоков — гонка данных. thread_local даёт каждому потоку свой собственный
// экземпляр без единой блокировки.
std::mt19937& rng() {
    static thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

}  // namespace

PlayerId Civilian::vote(const GameView& view) {
    std::vector<PlayerId> candidates;
    candidates.reserve(view.alive_players.size());
    for (const auto& player : view.alive_players) {
        if (player->id() != id_) {
            candidates.push_back(player->id());
        }
    }
    if (candidates.empty()) {
        return kNoTarget;
    }
    std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng())];
}

NightAction Civilian::act(const GameView&) {
    return NightAction{};  // ActionType::None: мирный житель ночью бездействует
}

}  // namespace mafia::roles
