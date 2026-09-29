/**********************************************************************
 * @file   SlotMap.hpp
 * @brief  template definition of class SlotMap
 * 
 * @author ichi-raven
 * @date   November 2023
 *********************************************************************/

#ifndef SLOTMAP_HPP_
#define SLOTMAP_HPP_

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>

#include "Exception.hpp"

//! you can change this but you shouldn't
using IDType = std::uint64_t;
//! the invalid value of handles
constexpr IDType kInvalidID = 0xFFFFFFFFFFFFFFFF;
//! default page allcoation size
constexpr std::size_t kDefaultPageSize = 32;
//! default allocator Type
using DefaultAllocator = std::allocator<std::byte>;

/**
 * @brief  trait to determine if template argument N is a power of 2
 */
template <IDType N, typename = std::is_integral<IDType>::type>
struct IsPowerOf2
{
    static constexpr bool value = (((N - 1U) & N) == 0) && N > 0;
};

//! forward declaration
template <typename T, std::size_t PageSize, typename Allocator, typename IsPowerOf2>
class Pool;

/**
 * @brief  class representing the handle to the object stored in the Pool
 * 
 * @tparam T objects type
 * @PageSize  Pool page size
 * @Allocator Pool allocator
 */
template <typename T, std::size_t PageSize = kDefaultPageSize, typename Allocator = DefaultAllocator>
class Handle
{
protected:
    using PoolType = Pool<T, PageSize, Allocator, std::enable_if_t<IsPowerOf2<PageSize>::value>>;
    friend PoolType;

public:
    /**
     * @brief  constructor
     */
    Handle()
        : mID(kInvalidID)
        , mpPool(nullptr)
    {
    }

    /**
     * @brief  destructor
     */
    virtual ~Handle()
    {
    }

    /**
     * @brief  copy constructor
     */
    Handle(const Handle& other)
    {
        this->mID    = other.mID;
        this->mpPool = other.mpPool;
    }

    /**
     * @brief  assignment operator overload (copy)
     */
    Handle& operator=(const Handle& other)
    {
        this->mID    = other.mID;
        this->mpPool = other.mpPool;

        return *this;
    }

    /**
     * @brief  move constructor
     */
    Handle(Handle&& other) noexcept
    {
        this->mID    = other.mID;
        this->mpPool = other.mpPool;

        other.mID = kInvalidID;
    }

    /**
     * @brief  assignment operator overload (move)
     */
    Handle& operator=(Handle&& other) noexcept
    {
        this->mID    = other.mID;
        this->mpPool = other.mpPool;

        other.mID = kInvalidID;

        return *this;
    }

    /**
     * @brief  obtain a raw ID on the Pool
     */
    IDType getRawID() const
    {
        return mID;
    }

    /**
     * @brief  bool operator overload to determine if the ID is normal (active at the end of the handle)
     */
    explicit operator bool() const noexcept
    {
        return mID != kInvalidID;
    }

    /**
     * @brief  not operator overload to determine if the ID is normal (active at the end of the handle)
     */
    bool operator!() const noexcept
    {
        return !static_cast<bool>(*this);
    }

    /**
     * @brief  obtain a reference to the object pointed to by the handle from the Pool
     */
    T& get() const
    {
        assert(mID != kInvalidID || !"invalid handle!");
        return mpPool->get(*this);
    }

    /**
     * @brief  arrow operator overloading of the object pointed to
     */
    T* operator->() const
    {
        assert(mID != kInvalidID || !"invalid handle!");
        return &mpPool->get(*this);
    }

protected:
    /**
     * @brief  internal constructor
     */
    Handle(IDType id, PoolType* pPool)
        : mID(id)
        , mpPool(pPool)
    {
    }

    //! ID(Nbit) = activeFlag(1bit) | slot(N / 2 - 1bit) | index(N / 2bit)
    IDType mID;
    //! pointer to Pool
    PoolType* mpPool;
};

/**
 * @brief  Handle's no copying & RAII version
 */
template <typename T, std::size_t PageSize = kDefaultPageSize, typename Allocator = DefaultAllocator>
class UniqueHandle : public Handle<T, PageSize, Allocator>
{
    using HandleType = Handle<T, PageSize, Allocator>;

public:
    /**
     * @brief  constructor
     */
    UniqueHandle()
        : HandleType()
    {
    }

    /**
     * @brief  destructor
     */
    virtual ~UniqueHandle()
    {
        this->reset();
    }

    // noncopyable (can't use macro)
    UniqueHandle(const UniqueHandle& other)            = delete;
    UniqueHandle& operator=(const UniqueHandle& other) = delete;

    /**
     * @brief  move constructor
     */
    explicit UniqueHandle(HandleType&& other) noexcept
        : HandleType(std::move(other))
    {
    }

    /**
     * @brief  assignment operator overload (move)
     */
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            HandleType::operator=(std::move(other));
        }
        return *this;
    }

    /**
     * @brief  assignment operator overload (move)
     */
    UniqueHandle& operator=(HandleType&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            HandleType::operator=(std::move(other));
        }
        return *this;
    }

    /**
     * @brief reset the handle and release the object
     */
    void reset() noexcept
    {
        if (this->mID != kInvalidID)
        {
            this->mpPool->deallocate(*this);
            this->mID = kInvalidID;
        }
    }

    /**
     * @brief  operator overloading as Handle type
     */
    explicit operator HandleType() const noexcept
    {
        assert(this->mID != kInvalidID || !"invalid handle!");
        return HandleType(this->mID, this->mpPool);
    }

protected:
    /**
     * @brief  internal constructor
     */
    UniqueHandle(IDType id, typename HandleType::PoolType* pPool)
        : HandleType(id, pPool)
    {
    }
};

/**
 * @brief  class that actually stores the object, accessible to each object via Handle
 * 
 * @tparam T objects type
 * @PageSize  Pool page size
 * @Allocator Pool allocator
 */
template <typename T, std::size_t PageSize = kDefaultPageSize, typename Allocator = DefaultAllocator,
          typename = std::enable_if_t<IsPowerOf2<PageSize>::value>>
class Pool
{
    using HandleType       = Handle<T, PageSize, Allocator>;
    using UniqueHandleType = UniqueHandle<T, PageSize, Allocator>;

public:
    /**
     * @brief  constructor
     */
    Pool()
    {
    }

    /**
     * @brief  destructor
     */
    ~Pool()
    {
        clear();
    }

    //! noncopyable (can't use macro)
    Pool(const Pool<T, PageSize>& other)                         = delete;
    Pool<T, PageSize>& operator=(const Pool<T, PageSize>& other) = delete;

    // nonmoveable (can't use macro)
    Pool(Pool<T, PageSize>&& other)                         = delete;
    Pool<T, PageSize>& operator=(Pool<T, PageSize>&& other) = delete;

    /**
     * @brief  allocating objects on the page
     * 
     * @tparam Args type forwarded to objects constructor
     * @param args arguments forwarded to objects constructor
     * @return Handle of an allocated object
     */
    template <typename... Args>
    HandleType allocate(Args&&... args)
    {
        return HandleType(allocInternal(std::forward<Args>(args)...), this);
    }

    /**
     * @brief  releases the object of the specified handle
     * 
     * @param handle pointing to the object to be released
     */
    bool deallocate(HandleType& handle)
    {
        const bool res = deallocInternal(handle.getRawID());

        handle.mID = kInvalidID;
        return res;
    }

    /**
     * @brief  obtains a reference to the object pointed to by the specified handle
     * 
     * @param handle pointing to the object
     */
    T& get(HandleType handle)
    {
        return getInternal(handle.getRawID());
    }

    /**
     * @brief  execute func for all stored objects
     * @detail if you have a few handles, you should use get() directly
     * 
     * @tparam Func type of function to execute
     * @param func function to execute
     */
    template <typename Func>
    void forEach(Func func)
    {
        for (auto [pPage, activeHandleNum] : mPageTable)
        {
            // skip empty page
            if (activeHandleNum == 0)
            {
                continue;
            }

            const IDType* IDptr    = reinterpret_cast<IDType*>(pPage + kTypeOffset);
            T* objectPtr           = reinterpret_cast<T*>(pPage);
            constexpr uint64_t MSB = (static_cast<IDType>(1) << (kShiftLength * 2 - 1));
            for (std::size_t i = 0; i < PageSize; ++i)
            {
                if (IDptr[i] & MSB)  // if active
                {
                    func(objectPtr[i]);
                }
            }
        }
    }

    /**
     * @brief  delete all objects
     */
    void clear()
    {
        forEach([](const T& val) { val.~T(); });

        for (auto [pPage, activeHandleNum] : mPageTable)
        {
            mAllocator.deallocate(pPage, kPageByteSize);
        }

        mPageTable.clear();
        mFreePosTable.clear();
    }

private:
    /**
     * @brief  internal implementation of the allocate function
     */
    template <typename... Args>
    IDType allocInternal(Args&&... args)
    {
        if (mFreePosTable.empty())
        {
            std::byte* pPage = mAllocator.allocate(kPageByteSize);

            for (std::size_t i = 0; i < PageSize; ++i)
            {
                IDType id                                                            = mPageTable.size() * PageSize + i;
                *reinterpret_cast<IDType*>(pPage + kTypeOffset + sizeof(IDType) * i) = id;
                mFreePosTable.emplace_back(id);
            }

            mPageTable.emplace_back(pPage, 0);
        }

        // register ID
        const auto id = mFreePosTable.back();
        mFreePosTable.pop_back();

        //constexpr uint32_t MSB = getMSB(PageSize);
        const auto div = id / PageSize;  // id >> MSB;
        const auto mod = id % PageSize;  // (id << (kShiftLength - MSB)) >> (kShiftLength - MSB);

        // increment active handle num in page
        ++mPageTable[div].second;

        // construct
        {
            new (mPageTable[div].first + sizeof(T) * mod) T(std::forward<Args>(args)...);
        }

        auto pTargetID = reinterpret_cast<IDType*>(mPageTable[div].first + kTypeOffset + sizeof(IDType) * mod);
        // activate flag
        *pTargetID |= (static_cast<IDType>(1) << (kShiftLength * 2 - 1));

        return *pTargetID;
    }

    /**
     * @brief  internal implementation of the deallocate function
     */
    bool deallocInternal(const IDType id)
    {
        const auto maskedID = id & kMask;
        const auto div      = maskedID / PageSize;
        const auto mod      = maskedID % PageSize;

        if (div >= mPageTable.size())  // invalid handle
        {
            return false;
        }

        // rotate slot
        {
            auto* pObjectID = reinterpret_cast<IDType*>(mPageTable[div].first + kTypeOffset + sizeof(IDType) * mod);
            if (*pObjectID != id)
            {
                return false;
            }

            T* ptr = reinterpret_cast<T*>(mPageTable[div].first + sizeof(T) * mod);
            ptr->~T();

            *pObjectID = (*pObjectID & kMask) | (((*pObjectID >> kShiftLength) + 1) << kShiftLength);

            // deactivate flag
            *pObjectID &= ~(static_cast<IDType>(1) << (kShiftLength * 2 - 1));
        }

        // remove active ID and return to be free
        mFreePosTable.emplace_back(maskedID);

        // decrement active page handle num
        --mPageTable[div].second;

        return true;
    }

    /**
     * @brief  internal implementation of the get function
     */
    T& getInternal(const IDType id)
    {
        if (id == kInvalidID)
        {
            throw vk2s::VkException("invalid handle");
        }

        const uint32_t maskedID = static_cast<uint32_t>(id & kMask);
        const auto div          = maskedID / PageSize;  // maskedID >> MSB;
        const auto mod          = maskedID % PageSize;  // (maskedID << (32 - MSB)) >> (32 - MSB);

        if (div >= mPageTable.size())
        {
            throw vk2s::VkException("handle points outside the pool");
        }

        const IDType storedID = *reinterpret_cast<IDType*>(mPageTable[div].first + kTypeOffset + sizeof(IDType) * mod);
        if (storedID != id)
        {
            throw vk2s::VkException("stale handle: the object has been released and reallocated");
        }

        return *reinterpret_cast<T*>(mPageTable[div].first + sizeof(T) * mod);
    }

    //! byte size per page
    constexpr static std::size_t kTypeOffset = PageSize * sizeof(T);
    //! byte size of each page
    constexpr static std::size_t kPageByteSize = kTypeOffset + PageSize * sizeof(IDType);
    //! bit shift length to slot section
    constexpr static std::size_t kShiftLength = sizeof(IDType) * 4;  // 4 means (* 8 / 2)
    //! bit mask to extract the slot portion
    constexpr static IDType kMask = (static_cast<IDType>(1) << kShiftLength) - 1;

    //! specified allocator
    Allocator mAllocator;
    //! table that holds pairs of pointers to pages and the current number of pages stored
    std::vector<std::pair<std::byte*, std::uint32_t>> mPageTable;
    //! vector to store the position on the released page
    std::vector<IDType> mFreePosTable;
};

#endif
