#pragma once

#include <string>
#include <utility>

#include "mafia/action.hpp"
#include "mafia/coroutines.hpp"
#include "mafia/role.hpp"

namespace mafia {

// Полное определение — в game_view.hpp. Здесь достаточно неполного типа:
// Player использует GameView только как параметр по ссылке, а для этого
// компилятору не нужно знать его размер/поля.
struct GameView;

// Базовый класс любого игрока. Конкретные роли (Civilian, Mafia, Doctor, ...)
// наследуются от него и реализуют act()/vote() согласно своей роли.
//
// Методы называются именно act()/vote() — это те сигнатуры, которые в
// будущем (опциональный пункт задания) будет проверять C++20-концепт,
// гарантирующий, что класс роли реализует необходимые для игры методы.
class Player {
public:
    Player(PlayerId id, std::string name) : id_(id), name_(std::move(name)) {}
    virtual ~Player() = default;

    // Игроки всегда живут за SharedPtr и никогда не копируются — копирование
    // полиморфного базового класса по значению обрезало бы (object slicing)
    // производный объект до Player, теряя роль. Явно запрещаем это на
    // этапе компиляции, а не полагаемся на то, что никто так не напишет.
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    PlayerId id() const noexcept { return id_; }
    const std::string& name() const noexcept { return name_; }

    bool is_alive() const noexcept { return alive_; }
    void kill() noexcept { alive_ = false; }  // применяется только GameMaster'ом

    // Управляется ли этот игрок человеком за консолью, а не ботом. По
    // умолчанию false (бот); main.cpp включает это для ровно одного игрока
    // в режиме --interactive. Конкретные роли сами решают в act()/vote(),
    // спрашивать ли консоль вместо случайного выбора — см. mafia/roles/role_utils.hpp.
    bool is_interactive() const noexcept { return interactive_; }
    void set_interactive(bool value = true) noexcept { interactive_ = value; }

    virtual Role role() const noexcept = 0;
    Team team() const noexcept { return team_of(role()); }

    // Дневное действие: выбрать, против кого голосовать.
    virtual PlayerId vote(const GameView& view) = 0;

    // Ночное действие: конкретный смысл (лечить/проверить/убить) зависит
    // от роли, но сигнатура одна и та же для всех игроков.
    virtual NightAction act(const GameView& view) = 0;

    // Корутинные варианты тех же ходов (режим --coroutines). По умолчанию
    // просто оборачивают синхронные vote()/act(): бот решает сразу, без
    // ожидания. Роли переопределяют их, чтобы интерактивный игрок мог
    // приостановиться на вводе (co_await), не блокируя остальных.
    virtual Task<PlayerId> vote_async(const GameView& view) { co_return vote(view); }
    virtual Task<NightAction> act_async(const GameView& view) { co_return act(view); }

protected:
    PlayerId id_;
    std::string name_;
    bool alive_ = true;
    bool interactive_ = false;
};

}  // namespace mafia
