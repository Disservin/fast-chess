#pragma once

#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <mutex>

#include <core/memory/scope_guard.hpp>

namespace fastchess::util {

// An entry for the cache pool, should be guarded by a ScopeGuard to release the entry
// when it goes out of scope and make it available for other threads to use again.
template <typename T, typename ID>
class CachedEntry : public ScopeEntry {
   public:
    template <typename... ARGS>
    CachedEntry(const ID& identifier, ARGS&&... arg)
        : ScopeEntry(false), entry_(std::make_unique<T>(std::forward<ARGS>(arg)...)), id(identifier) {}

    T* operator->() noexcept { return entry_.get(); }
    const T* operator->() const noexcept { return entry_.get(); }

    [[nodiscard]] auto& get() noexcept { return entry_; }

   private:
    std::unique_ptr<T> entry_;
    ID id;

    template <typename TT, typename II>
    friend class CachePool;
};

// CachePool is a pool of objects which can be reused. An object is indentified by an ID
// type and value. The pool is thread safe.
template <typename T, typename ID>
class CachePool {
   public:
    template <typename... ARGS>
    [[nodiscard]] auto getEntry(const ID& identifier, ARGS&&... arg) {
        std::lock_guard<std::mutex> lock(access_mutex_);

        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->available_ && it->id == identifier) {
                // block the entry from being used by other threads
                it->available_ = false;
                return it;
            }
            ++it;
        }

        cache_.emplace_back(identifier, std::forward<ARGS>(arg)...);
        return std::prev(cache_.end());
    }

    void deleteFromCache(const typename std::list<CachedEntry<T, ID>>::iterator& it) {
        assert(it != cache_.end());
        assert(!it->available_);
        std::lock_guard<std::mutex> lock(access_mutex_);
        cache_.erase(it);
    }

    void loopEntries(const std::function<void(typename std::list<CachedEntry<T, ID>>::iterator&)>& func) {
        std::lock_guard<std::mutex> lock(access_mutex_);
        for (auto it = cache_.begin(); it != cache_.end(); ++it) { 
            func(it);
        }
    }

   private:
    std::list<CachedEntry<T, ID>> cache_;
    std::mutex access_mutex_;
};

}  // namespace fastchess::util
