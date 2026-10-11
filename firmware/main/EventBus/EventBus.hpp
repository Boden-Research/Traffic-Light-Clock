#pragma once

#include <iostream>
#include <functional>
#include <unordered_map>
#include <vector>
#include <algorithm>

/*
  # EventBus
  
  An EventBus is a system-wide pub/sub architecture for distributing events to various subscribers.
  Clients subscribe (sub) to an event, and servers publish (pub) events out.
  Each event that is published will be received by all subscriptions to that event.

  Events pass along a data type.
  These data types *may* be as simple as an int, byte or void, or may be as complex as a full typedef struct.
  Event data is ALWAYS passed by pointer.
  If no event data is to be passed, declare the EventBus variable using the type "void", and call Publish() with nullptr.

  When subscribing, call Subscribe() and you will receive a EventSubscription_t value back.
  This is the value that describes your subscription.

  To unsubscribe, call Unsubscribe, passing the EventSubscription_t value you were given.
  Your callback function will be removed from the subscribers list.

  To send out a new event, call Publish(), giving it the event data type.

  Example publisher:
    #include "EventBus.hpp"

    typedef struct {
        int MyEventData;
    } MyEvent_t;

    // Instance of the event bus that subscribers will access
    EventBus<MyEvent_t> MyEvent;

    void OnMyEvent(int myEventData) {
        // Publish the event
        MyEvent::Publish((MyEvent_t) { myEventData });
    }

  Example subscriber:
    #include "EventBus.hpp"
    #include "MyEvent.hpp"

    static EventSubscription_t _myEventSub;

    void MyEventHandler(const MyEvent_t& event) {
        // We have received an event
        ESP_LOGI(TAG, "Got event \"MyEvent_t\" with data %d", event.MyEventData);
    }

    void SubscribeToMyEvent() {
        // Subscribe to the event, saving the subscription handle
        _myEventSub = MyEvent::Subscribe(MyEventHandler);
    }

    void UnsubscribeFromMyEvent() {
        // Unsubscribe from the event using the subscription handle
        MyEvent::Unsubscribe(_myEventSub);

        // Zero out the subscription handle; not technically needed, but good practice
        _myEventSub = 0;
    }
*/

// A unique ID used to unregister a subscriber later
using EventSubscription_t = size_t;

template <typename EventType>
class EventBus {
    public:
        // The callback signature matches the EventType's payload
        using Callback = std::function<void(const EventType*)>;

        // Subscribes to the event and returns a unique ID for unsubscription
        EventSubscription_t Subscribe(Callback callback) {
            static EventSubscription_t nextId = 0;
            EventSubscription_t id = ++nextId;
            GetListeners()[id] = callback;
            return id;
        }

        // Unsubscribes a listener using its ID
        void Unsubscribe(EventSubscription_t id) {
            GetListeners().erase(id);
        }

        // Publishes an event to all registered listeners
        void Publish(const EventType* event) {
            for (const auto& [id, callback] : GetListeners())
                if (callback)
                    callback(event);
        }

    private:
        // Using a static function variable avoids the static initialization order fiasco
        std::unordered_map<EventSubscription_t, Callback>& GetListeners() {
            static std::unordered_map<EventSubscription_t, Callback> listeners;
            return listeners;
        }
};
