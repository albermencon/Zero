#pragma once
#include <cstdint>
#include <iosfwd> // Forward declaration for std::ostream

namespace VoxelEngine
{

    // Event types used for casting/checking
    enum class EventType : uint8_t
    {
        None = 0,
        WindowClose,
        WindowResize,
        WindowFocus,
        WindowLostFocus,
        WindowMoved,
        KeyPressed,
        KeyReleased,
        KeyTyped,
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseScrolled
    };

    // Bitfield for filtering events (e.g., "Give me all Input events")
    enum EventCategory : uint8_t
    {
        None = 0,
        EventCategoryApplication = 1 << 0,
        EventCategoryInput = 1 << 1,
        EventCategoryKeyboard = 1 << 2,
        EventCategoryMouse = 1 << 3,
        EventCategoryMouseButton = 1 << 4
    };

#define BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

    // Macros to reduce boilerplate in subclasses
#define EVENT_CLASS_TYPE(type)                                                  \
    static EventType GetStaticType() { return EventType::type; }                \
    virtual EventType GetEventType() const override { return GetStaticType(); } \
    virtual const char *GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) \
    virtual int GetCategoryFlags() const override { return category; }

    /**
     * @class Event
     * @brief Base class for all events in the VoxelEngine.
     *
     * The Event class provides a common interface for all events in the engine.
     * Events are not inherently blocking; however, they can be marked as handled
     * using the `Handled` flag to stop further propagation.
     *
     * Subclasses must implement the following pure virtual methods:
     * - GetEventType(): Returns the specific type of the event.
     * - GetName(): Returns the name of the event as a string.
     * - GetCategoryFlags(): Returns the category flags for filtering events.
     *
     * The `ToString` method can be overridden to provide a string representation
     * of the event for debugging or logging purposes.
     */
    class ENGINE_API Event
    {
    public:
        virtual ~Event() = default;

        bool Handled = false; // If true, stop propagating this event

        virtual EventType GetEventType() const = 0;
        virtual const char *GetName() const = 0;
        virtual int GetCategoryFlags() const = 0;
        virtual std::string ToString() const;

        inline bool IsInCategory(EventCategory category)
        {
            return GetCategoryFlags() & category;
        }
    };

    // Utility to log events easily
    std::ostream &operator<<(std::ostream &os, const Event &e);

}
