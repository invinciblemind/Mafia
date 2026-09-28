#include "mafia/shared_ptr.hpp"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

namespace {

struct Probe {
    static inline int alive = 0;
    int value;
    explicit Probe(int v) : value(v) { ++alive; }
    ~Probe() { --alive; }
};

void test_construction_and_dereference() {
    mafia::SharedPtr<Probe> p(new Probe(42));
    assert(p.use_count() == 1);
    assert((*p).value == 42);
    assert(p->value == 42);
    assert(Probe::alive == 1);
}

void test_copy_shares_ownership() {
    mafia::SharedPtr<Probe> a(new Probe(1));
    {
        mafia::SharedPtr<Probe> b = a;
        assert(a.use_count() == 2);
        assert(b.use_count() == 2);
        assert(a.get() == b.get());
    }
    assert(a.use_count() == 1);
}

void test_move_transfers_ownership() {
    mafia::SharedPtr<Probe> a(new Probe(2));
    Probe* raw = a.get();
    mafia::SharedPtr<Probe> b = std::move(a);
    assert(a.get() == nullptr);
    assert(b.get() == raw);
    assert(b.use_count() == 1);
}

void test_reset_and_swap() {
    mafia::SharedPtr<Probe> a(new Probe(3));
    mafia::SharedPtr<Probe> b(new Probe(4));
    a.swap(b);
    assert(a->value == 4);
    assert(b->value == 3);

    a.reset();
    assert(!a);
    assert(a.get() == nullptr);

    a.reset(new Probe(5));
    assert(a->value == 5);
}

void test_comparisons() {
    mafia::SharedPtr<Probe> a(new Probe(6));
    mafia::SharedPtr<Probe> b = a;
    mafia::SharedPtr<Probe> c(new Probe(7));
    assert(a == b);
    assert(a != c);
    assert(a == a);
    mafia::SharedPtr<Probe> n;
    assert(n == nullptr);
    assert(a != nullptr);
}

void test_no_leaks_after_scope() {
    assert(Probe::alive == 0);
    {
        mafia::SharedPtr<Probe> a(new Probe(8));
        mafia::SharedPtr<Probe> b = a;
        mafia::SharedPtr<Probe> c(new Probe(9));
    }
    assert(Probe::alive == 0);
}

void test_thread_safety() {
    auto shared = mafia::make_shared_ptr<Probe>(10);
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([shared]() mutable {
            for (int j = 0; j < 1000; ++j) {
                mafia::SharedPtr<Probe> copy = shared;
                (void)copy->value;
            }
        });
    }
    for (auto& t : threads) t.join();
    assert(shared.use_count() == 1);
}

}  // namespace

int main() {
    test_construction_and_dereference();
    test_copy_shares_ownership();
    test_move_transfers_ownership();
    test_reset_and_swap();
    test_comparisons();
    test_no_leaks_after_scope();
    test_thread_safety();
    std::cout << "All SharedPtr tests passed.\n";
    return 0;
}
