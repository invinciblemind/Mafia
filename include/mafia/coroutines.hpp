#pragma once

#include <coroutine>
#include <deque>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mafia {

namespace detail {

struct TaskPromiseBase {
    // Кто ждёт эту корутину (co_await task). Для корутины верхнего уровня —
    // noop: по завершении управление просто вернётся тому, кто вызвал resume().
    std::coroutine_handle<> continuation = std::noop_coroutine();
    std::exception_ptr error;

    // Ленивый старт: корутина не выполняется, пока её не запустит планировщик
    // (resume) или не дождётся другая корутина (co_await).
    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        // Symmetric transfer: по завершении сразу передаём управление ждавшей
        // корутине, не наращивая стек вызовов.
        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            return handle.promise().continuation;
        }
        void await_resume() noexcept {}
    };
    FinalAwaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() { error = std::current_exception(); }
};

}  // namespace detail

// Ленивая корутина, возвращающая значение T. Владеет своим кадром.
template <typename T>
class Task {
public:
    struct promise_type : detail::TaskPromiseBase {
        std::optional<T> value;

        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        void return_value(T result) { value = std::move(result); }
    };

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { destroy(); }

    bool done() const noexcept { return handle_.done(); }
    void resume() { handle_.resume(); }

    // Результат завершённой корутины; исключение, вылетевшее из неё, пробрасывается сюда.
    T result() {
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
        return std::move(*handle_.promise().value);
    }

    // co_await на Task: запускает вложенную корутину и возобновляет ждущую
    // по её завершении.
    auto operator co_await() && noexcept {
        struct Awaiter {
            std::coroutine_handle<promise_type> handle;
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
                handle.promise().continuation = awaiting;
                return handle;
            }
            T await_resume() {
                if (handle.promise().error) {
                    std::rethrow_exception(handle.promise().error);
                }
                return std::move(*handle.promise().value);
            }
        };
        return Awaiter{handle_};
    }

private:
    void destroy() noexcept {
        if (handle_) {
            handle_.destroy();
        }
    }

    std::coroutine_handle<promise_type> handle_;
};

// Канал связи с человеком за консолью. Корутина игрока делает
// `co_await input.read_line()` и приостанавливается; сама строка читается
// только когда планировщику больше нечего выполнять (см. run_all) — поэтому
// ожидание ввода человека не задерживает ботов.
class InputBroker {
public:
    InputBroker() = default;
    InputBroker(std::istream& in, std::ostream& out) : in_(&in), out_(&out) {}

    void set_streams(std::istream& in, std::ostream& out) {
        in_ = &in;
        out_ = &out;
    }
    std::ostream& out() { return *out_; }

    struct ReadLineAwaiter {
        InputBroker& broker;
        std::optional<std::string> line;  // nullopt — поток ввода исчерпан (EOF)

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> waiting) { broker.pending_.push_back({waiting, this}); }
        std::optional<std::string> await_resume() { return std::move(line); }
    };
    ReadLineAwaiter read_line() { return ReadLineAwaiter{*this, std::nullopt}; }

    bool has_pending() const noexcept { return !pending_.empty(); }

    // Читает строку для самой давней ждущей корутины и возобновляет её.
    // Это ЕДИНСТВЕННОЕ место, где программа может заблокироваться на вводе.
    void serve_one() {
        if (pending_.empty()) {
            throw std::logic_error("InputBroker::serve_one: нет корутин, ожидающих ввода");
        }
        Pending next = pending_.front();
        pending_.pop_front();

        std::string text;
        if (std::getline(*in_, text)) {
            next.awaiter->line = std::move(text);
        }
        next.handle.resume();
    }

private:
    struct Pending {
        std::coroutine_handle<> handle;
        ReadLineAwaiter* awaiter;
    };

    std::istream* in_ = &std::cin;
    std::ostream* out_ = &std::cout;
    std::deque<Pending> pending_;
};

// Кооперативный планировщик в одном потоке. Каждая корутина получает управление
// и работает до завершения или до ожидания ввода. Боты ждать ничего не
// умеют, поэтому завершаются сразу; корутины, которые встали на ввод,
// обслуживаются потом по одной, пока ожидающих не останется.
template <typename T>
std::vector<T> run_all(std::vector<Task<T>> tasks, InputBroker& input) {
    for (Task<T>& task : tasks) {
        task.resume();
    }
    while (input.has_pending()) {
        input.serve_one();
    }

    std::vector<T> results;
    results.reserve(tasks.size());
    for (Task<T>& task : tasks) {
        if (!task.done()) {
            throw std::logic_error("run_all: корутина не завершилась и не ждёт ввода");
        }
        results.push_back(task.result());
    }
    return results;
}

// Синхронно доводит одну корутину до конца (для режима нитей: человек
// отвечает в своей нити, и блокироваться на вводе ему можно).
template <typename T>
T run_blocking(Task<T> task, InputBroker& input) {
    task.resume();
    while (!task.done()) {
        input.serve_one();
    }
    return task.result();
}

}  // namespace mafia
