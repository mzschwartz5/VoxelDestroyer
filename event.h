#pragma once

#include <functional>
#include <unordered_map>
#include <atomic>

class EventBase {
public:
    using Unsubscribe = std::function<void()>;
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