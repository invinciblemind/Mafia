#pragma once

#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <utility>

namespace mafia {

namespace detail {

// Не зависит от T, поэтому вынесен из шаблона SharedPtr в отдельный тип:
// это позволяет SharedPtr<Derived> и SharedPtr<Base> ссылаться на один и тот
// же по типу control-block (иначе, будучи вложенным в шаблон, control_
// каждой инстанциации SharedPtr<T> имел бы свой собственный несовместимый
// тип ControlBlock, и конвертация между ними не скомпилировалась бы).
struct SharedPtrControlBlock {
    std::atomic<long> count;
    explicit SharedPtrControlBlock(long initial) : count(initial) {}
};

}  // namespace detail

// Собственная упрощённая реализация shared_ptr с атомарным счётчиком ссылок,
// чтобы её можно было безопасно копировать между игровыми потоками (std::thread).
template <typename T>
class SharedPtr {
public:
    // Разрешаем SharedPtr<T> заглядывать в приватные поля SharedPtr<U> для
    // любых U — это нужно конвертирующим конструктору/оператору ниже
    // (например, SharedPtr<Player> из SharedPtr<Civilian>): без дружбы
    // между разными инстанциациями шаблона control_/ptr_ были бы недоступны.
    template <typename U>
    friend class SharedPtr;

    SharedPtr() noexcept = default;

    SharedPtr(std::nullptr_t) noexcept {}

    // ВНИМАНИЕ (как и у std::shared_ptr): каждый вызов этого конструктора
    // создаёт СВОЙ, отдельный control block. Если обернуть один и тот же
    // сырой указатель в SharedPtr дважды (а не скопировать уже существующий
    // SharedPtr), оба экземпляра будут независимо считать себя единственным
    // владельцем и оба в итоге вызовут delete на одном объекте — double
    // free. Единственный источник владения должен быть один: используйте
    // либо make_shared_ptr(...), либо копируйте/перемещайте уже имеющийся
    // SharedPtr, но никогда не оборачивайте один T* повторно.
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

    // Конвертирующие конструкторы: разрешают SharedPtr<Derived> -> SharedPtr<Base>
    // (например, SharedPtr<Civilian> -> SharedPtr<Player>), ровно там, где это
    // разрешил бы обычный сырой указатель (Derived* -> Base*). requires не даёт
    // случайно "сконвертировать" несвязанные типы, скажем SharedPtr<int> в
    // SharedPtr<double>.
    template <typename U>
        requires std::convertible_to<U*, T*>
    SharedPtr(const SharedPtr<U>& other) noexcept : ptr_(other.ptr_), control_(other.control_) {
        acquire();
    }

    template <typename U>
        requires std::convertible_to<U*, T*>
    SharedPtr(SharedPtr<U>&& other) noexcept : ptr_(other.ptr_), control_(other.control_) {
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
    using ControlBlock = detail::SharedPtrControlBlock;

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
