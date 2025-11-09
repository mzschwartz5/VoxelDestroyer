#pragma once

#include <functional>
#include <unordered_map>
#include <atomic>

class EventBase {
public:
    // Wrapper around std::function<void()> that is safe to call even if default-constructed,
    // and safe to call multiple times.
    class Unsubscribe {
    public:
        Unsubscribe() noexcept = default;
        Unsubscribe(std::function<void()> f) : fn(std::move(f)) {}

        void operator()() noexcept {
            if (!fn) return;

            auto f = std::move(fn); // move out so call only works once.
            f();                    // invoke once
        }

        bool has_value() const noexcept { return static_cast<bool>(fn); }
        explicit operator bool() const noexcept { return has_value(); }

    private:
        mutable std::function<void()> fn;
    };
};

/**
 * A basic event system template class. Create a subscription and notify listeners.
 * FYI this isn't meant to be thread-safe.
 */
template<typename... Args>
class Event : public EventBase {
public:
    using Listener = std::function<void(Args...)>;

    EventBase::Unsubscribe subscribe(const Listener& listener) {
        int listenerId = nextListenerId++;
        listeners[listenerId] = listener;
        return [listenerId, this]() {
            listeners.erase(listenerId);
        };
    }

    void notify(Args... args) {
        for (const auto& listener : listeners) {
            listener.second(args...);
        }
    }

private:
    std::unordered_map<int, Listener> listeners;
    std::atomic<int> nextListenerId{ 0 };
};