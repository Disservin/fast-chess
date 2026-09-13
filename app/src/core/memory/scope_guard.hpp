#pragma once

#include <atomic>
#include <type_traits>

namespace fastchess::util {

// Base class for entries which are managed by a ScopeGuard.
// Needs to be inherited by the managed class.
class ScopeEntry {
   public:
    ScopeEntry(bool available) : available_(available) {}

    void release() noexcept { available_ = true; }
    bool isAvailable() const noexcept { return available_.load(); }

   protected:
    std::atomic<bool> available_;
};

// RAII class that releases the entry when it goes out of scope
template <typename T>
class ScopeGuard {
   public:
    explicit ScopeGuard(T* entry) : entry_(entry) {
        static_assert(std::is_base_of<ScopeEntry, T>::value,
                      "type parameter of this class must derive from ScopeEntry");
    }

    explicit ScopeGuard(T& entry) : entry_(&entry) {
        static_assert(std::is_base_of<ScopeEntry, T>::value,
                      "type parameter of this class must derive from ScopeEntry");
    }

    ~ScopeGuard() {
        if (entry_ == nullptr) return;
        entry_->release();
    }

    ScopeGuard(const ScopeGuard&)            = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    [[nodiscard]] auto& get() noexcept { return *entry_; }
    [[nodiscard]] auto& get() const noexcept { return *entry_; }

   private:
    T* entry_;
};

}  // namespace fastchess::util