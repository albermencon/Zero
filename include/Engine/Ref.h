#pragma once
#include <atomic>
#include <cstdint>
#include <utility>

namespace Zero
{
    class ReferenceCounted;

    struct WeakRefBlock
    {
        WeakRefBlock() = default;
        WeakRefBlock(const WeakRefBlock&) = delete;
        WeakRefBlock& operator=(const WeakRefBlock&) = delete;

        std::atomic<ReferenceCounted*> ptr{nullptr};
        std::atomic<uint32_t> weakRefCount{0};
        
        // Spinlock prevents a race where Thread A reads a valid ptr and increments 
        // the refcount (resurrection) while Thread B is in the middle of DecRefCount()
        // and is about to delete the object.
        std::atomic_flag lock = ATOMIC_FLAG_INIT;

        void Lock();
        void Unlock();
        ReferenceCounted* LockAndObtain();
    };

    class ReferenceCounted
    {
    public:
        void IncRefCount() const
        {
            m_refCount.fetch_add(1, std::memory_order_relaxed);
        }

        bool TryIncRefCount() const
        {
            uint32_t count = m_refCount.load(std::memory_order_relaxed);
            while (count != 0)
            {
                if (m_refCount.compare_exchange_weak(count, count + 1, std::memory_order_acquire, std::memory_order_relaxed))
                {
                    return true;
                }
            }
            return false;
        }

        void DecRefCount() const;

        uint32_t GetRefCount() const
        {
            return m_refCount.load(std::memory_order_relaxed);
        }

        // Precondition: The caller MUST hold a strong reference to this object before calling this.
        // Calling this without holding a strong reference is undefined behavior, as the object
        // could be concurrently destroyed during execution.
        WeakRefBlock* GetOrAllocateWeakBlock() const;

    protected:
        virtual ~ReferenceCounted() = default;

    private:
        mutable std::atomic<uint32_t> m_refCount{0};
        mutable std::atomic<WeakRefBlock*> m_weakBlock{nullptr};
    };

    template<typename T>
    class Ref
    {
    public:
        Ref() : m_ptr(nullptr) {}
        Ref(decltype(nullptr)) : m_ptr(nullptr) {}

        Ref(T* ptr) : m_ptr(ptr)
        {
            if (m_ptr) m_ptr->IncRefCount();
        }

        Ref(const Ref& other) : m_ptr(other.m_ptr)
        {
            if (m_ptr) m_ptr->IncRefCount();
        }

        template<typename U>
        Ref(const Ref<U>& other) : m_ptr(other.m_ptr)
        {
            if (m_ptr) m_ptr->IncRefCount();
        }

        Ref(Ref&& other) noexcept : m_ptr(other.m_ptr)
        {
            other.m_ptr = nullptr;
        }

        template<typename U>
        Ref(Ref<U>&& other) noexcept : m_ptr(other.m_ptr)
        {
            other.m_ptr = nullptr;
        }

        ~Ref()
        {
            if (m_ptr) m_ptr->DecRefCount();
        }

        Ref& operator=(const Ref& other)
        {
            if (this != &other)
            {
                if (other.m_ptr) other.m_ptr->IncRefCount();
                if (m_ptr) m_ptr->DecRefCount();
                m_ptr = other.m_ptr;
            }
            return *this;
        }

        template<typename U>
        Ref& operator=(const Ref<U>& other)
        {
            if (other.m_ptr) other.m_ptr->IncRefCount();
            if (m_ptr) m_ptr->DecRefCount();
            m_ptr = other.m_ptr;
            return *this;
        }

        Ref& operator=(Ref&& other) noexcept
        {
            if (this != &other)
            {
                if (m_ptr) m_ptr->DecRefCount();
                m_ptr = other.m_ptr;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        template<typename U>
        Ref& operator=(Ref<U>&& other) noexcept
        {
            if (static_cast<void*>(this) != static_cast<void*>(&other))
            {
                if (m_ptr) m_ptr->DecRefCount();
                m_ptr = other.m_ptr;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        Ref& operator=(decltype(nullptr))
        {
            if (m_ptr) m_ptr->DecRefCount();
            m_ptr = nullptr;
            return *this;
        }

        void Reset(T* ptr = nullptr)
        {
            if (m_ptr != ptr)
            {
                if (ptr) ptr->IncRefCount();
                if (m_ptr) m_ptr->DecRefCount();
                m_ptr = ptr;
            }
        }

        void Swap(Ref& other) noexcept
        {
            std::swap(m_ptr, other.m_ptr);
        }

        T* operator->() const { return m_ptr; }
        T& operator*() const { return *m_ptr; }
        explicit operator bool() const { return m_ptr != nullptr; }
        T* Get() const { return m_ptr; }

        template<typename U>
        Ref<U> As() const
        {
            return Ref<U>(dynamic_cast<U*>(m_ptr));
        }

        bool operator==(const Ref<T>& other) const { return m_ptr == other.m_ptr; }
        bool operator!=(const Ref<T>& other) const { return m_ptr != other.m_ptr; }
        bool operator==(decltype(nullptr)) const { return m_ptr == nullptr; }
        bool operator!=(decltype(nullptr)) const { return m_ptr != nullptr; }

    private:
        template<typename U> friend class Ref;
        template<typename U> friend class WeakRef;

        Ref(T* ptr, bool addRef) : m_ptr(ptr)
        {
            if (addRef && m_ptr)
                m_ptr->IncRefCount();
        }

        T* m_ptr;
    };

    template<typename T>
    void Swap(Ref<T>& lhs, Ref<T>& rhs) noexcept
    {
        lhs.Swap(rhs);
    }

    template<typename T, typename... Args>
    Ref<T> MakeRef(Args&&... args)
    {
        // Object is created with refCount = 0.
        // The Ref constructor takes ownership and increments to 1.
        return Ref<T>(new T(std::forward<Args>(args)...));
    }

    template<typename T>
    class WeakRef
    {
    public:
        WeakRef() : m_block(nullptr) {}

        WeakRef(const Ref<T>& ref)
        {
            Init(ref.Get());
        }

        WeakRef(T* ptr)
        {
            Init(ptr);
        }

        WeakRef(const WeakRef& other) : m_block(other.m_block)
        {
            if (m_block) m_block->weakRefCount.fetch_add(1, std::memory_order_relaxed);
        }

        template<typename U>
        WeakRef(const WeakRef<U>& other) : m_block(other.m_block)
        {
            if (m_block) m_block->weakRefCount.fetch_add(1, std::memory_order_relaxed);
        }

        WeakRef(WeakRef&& other) noexcept : m_block(other.m_block)
        {
            other.m_block = nullptr;
        }

        template<typename U>
        WeakRef(WeakRef<U>&& other) noexcept : m_block(other.m_block)
        {
            other.m_block = nullptr;
        }

        ~WeakRef()
        {
            Release();
        }

        WeakRef& operator=(const WeakRef& other)
        {
            if (this != &other)
            {
                if (other.m_block) other.m_block->weakRefCount.fetch_add(1, std::memory_order_relaxed);
                Release();
                m_block = other.m_block;
            }
            return *this;
        }

        template<typename U>
        WeakRef& operator=(const WeakRef<U>& other)
        {
            if (other.m_block) other.m_block->weakRefCount.fetch_add(1, std::memory_order_relaxed);
            Release();
            m_block = other.m_block;
            return *this;
        }

        WeakRef& operator=(WeakRef&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_block = other.m_block;
                other.m_block = nullptr;
            }
            return *this;
        }

        template<typename U>
        WeakRef& operator=(WeakRef<U>&& other) noexcept
        {
            if (static_cast<void*>(this) != static_cast<void*>(&other))
            {
                Release();
                m_block = other.m_block;
                other.m_block = nullptr;
            }
            return *this;
        }

        WeakRef& operator=(const Ref<T>& ref)
        {
            Release();
            Init(ref.Get());
            return *this;
        }

        void Reset()
        {
            Release();
            m_block = nullptr;
        }

        void Swap(WeakRef& other) noexcept
        {
            std::swap(m_block, other.m_block);
        }

        Ref<T> Lock() const
        {
            if (!m_block) return Ref<T>();
            ReferenceCounted* rawPtr = m_block->LockAndObtain();
            return Ref<T>(static_cast<T*>(rawPtr), false);
        }

        bool IsValid() const
        {
            if (!m_block) return false;
            return m_block->ptr.load(std::memory_order_relaxed) != nullptr;
        }

        explicit operator bool() const { return IsValid(); }

    private:
        template<typename U> friend class WeakRef;

        void Init(ReferenceCounted* ptr)
        {
            if (ptr)
            {
                m_block = ptr->GetOrAllocateWeakBlock();
                m_block->weakRefCount.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                m_block = nullptr;
            }
        }

        void Release()
        {
            if (m_block)
            {
                if (m_block->weakRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    delete m_block;
                }
            }
        }

        WeakRefBlock* m_block;
    };

    template<typename T>
    void Swap(WeakRef<T>& lhs, WeakRef<T>& rhs) noexcept
    {
        lhs.Swap(rhs);
    }
}
