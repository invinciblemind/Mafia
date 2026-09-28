#pragma once

#include <atomic>
#include <compare>
#include <cstddef>
#include <utility>

namespace mafia {

// Собственная упрощённая реализация shared_ptr с атомарным счётчиком ссылок,
// чтобы её можно было безопасно копировать между игровыми потоками (std::thread).
template <typename T>
class SharedPtr {
public:
    SharedPtr() noexcept = default;

    SharedPtr(std::nullptr_t) noexcept {}

    explicit SharedPtr(T* ptr) : ptr_(ptr) {
        if (ptr_) {
            control_ = new ControlBlock(1);
        }
    }

    SharedPtr(const SharedPtr& other) noexcept : ptr_(other.ptr_), control_(other.control_) {
        acquire();
    }

    SharedPtr(SharedPtr&& other) noexcept : ptr_(other.ptr_), control_(other.control_) {
        other.ptr_ = nullptr;
        other.control_ = nullptr;
    }

    ~SharedPtr() { release(); }

    SharedPtr& operator=(const SharedPtr& other) noexcept {
        if (this != &other) {
            SharedPtr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            control_ = other.control_;
            other.ptr_ = nullptr;
            other.control_ = nullptr;
        }
        return *this;
    }

    SharedPtr& operator=(std::nullptr_t) noexcept {
        release();
        ptr_ = nullptr;
        control_ = nullptr;
        return *this;
    }

    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    void reset(T* ptr = nullptr) {
        release();
        ptr_ = ptr;
        control_ = ptr_ ? new ControlBlock(1) : nullptr;
    }

    void swap(SharedPtr& other) noexcept {
        std::swap(ptr_, other.ptr_);
        std::swap(control_, other.control_);
    }

    T* get() const noexcept { return ptr_; }

    long use_count() const noexcept { return control_ ? control_->count.load() : 0; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    bool operator==(const SharedPtr& other) const noexcept { return ptr_ == other.ptr_; }
    bool operator==(std::nullptr_t) const noexcept { return ptr_ == nullptr; }
    std::strong_ordering operator<=>(const SharedPtr& other) const noexcept { return ptr_ <=> other.ptr_; }

private:
    struct ControlBlock {
        std::atomic<long> count;
        explicit ControlBlock(long initial) : count(initial) {}
    };

    T* ptr_ = nullptr;
    ControlBlock* control_ = nullptr;

    void acquire() noexcept {
        if (control_) {
            control_->count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void release() noexcept {
        if (control_ && control_->count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete ptr_;
            delete control_;
        }
        ptr_ = nullptr;
        control_ = nullptr;
    }
};

template <typename T, typename... Args>
SharedPtr<T> make_shared_ptr(Args&&... args) {
    return SharedPtr<T>(new T(std::forward<Args>(args)...));
}

}  // namespace mafia
