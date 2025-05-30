/*
MIT License

Copyright (c) 2021 Attila Csikós

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


/* Settings
* Use the following define-s before the header include

Node center is not stored within the nodes. It will be calculated ad-hoc every time when it is required, e.g in search algorithm.
#define ORTHOTREE__DISABLED_NODECENTER

Node size is not stored within the nodes. It will be calculated ad-hoc every time when it is required, e.g in search algorithm.
#define ORTHOTREE__DISABLED_NODESIZE

// PMR is used with MSVC only by default. To use PMR anyway
ORTHOTREE__USE_PMR

// To disable PMR on all platforms use:
ORTHOTREE__DISABLE_PMR

// Contiguous container of geometry data does not have specified index type. Octree lib uses index_t for it, it can specified to int or std::size_t.
ORTHOTREE_INDEX_T__INT / ORTHOTREE_INDEX_T__SIZE_T / ORTHOTREE_INDEX_T__UINT_FAST32_T

*/

#if defined(ORTHOTREE__USE_PMR) || defined(_MSC_VER)
#ifndef ORTHOTREE__DISABLE_PMR
#define IS_PMR_USED
#endif // !ORTHOTREE__DISABLE_PMR
#endif

#ifndef ORTHOTREE_GUARD
#define ORTHOTREE_GUARD

#include <assert.h>
#include <math.h>

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <concepts>
#include <cstring>
#include <execution>
#include <functional>
#include <iterator>
#include <map>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <stack>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <version>

#if (defined(__BMI2__) || defined(__AVX2__)) && (defined(_M_X64) || defined(__x86_64__) || defined(__amd64__))
#ifdef __has_include
#if __has_include(<immintrin.h>)
#include <immintrin.h>
#define BMI2_PDEP_AVAILABLE 1
#endif
#endif
#endif

#if defined(__clang__)
#define LOOPIVDEP
#elif defined(__INTEL_COMPILER)
#define LOOPIVDEP _Pragma("ivdep")
#elif defined(__GNUC__)
#define LOOPIVDEP _Pragma("GCC ivdep")
#elif defined(_MSC_VER)
#define LOOPIVDEP _Pragma("loop(ivdep)")
#else
#define LOOPIVDEP
#endif

#ifndef CRASH
#define CRASH_UNDEF
#define CRASH(errorMessage)        \
  do                               \
  {                                \
    assert(false && errorMessage); \
    std::terminate();              \
  } while (0)
#endif // !CRASH

#ifndef CRASH_IF
#define CRASH_IF_UNDEF
#define CRASH_IF(cond, errorMessage) \
  do                                 \
  {                                  \
    if (cond)                        \
    {                                \
      CRASH(errorMessage);           \
    }                                \
  } while (0)
#endif // !CRASH_IF

#ifdef __cpp_lib_execution
#define EXEC_POL_DEF(e) \
  std::conditional_t<IS_PARALLEL_EXEC, std::execution::parallel_unsequenced_policy, std::execution::unsequenced_policy> constexpr e
#define EXEC_POL_ADD(e) e,
#else
#define EXEC_POL_DEF(e)
#define EXEC_POL_ADD(e)
#endif

namespace OrthoTree
{
#ifdef ORTHOTREE_INDEX_T__SIZE_T
  using index_t = std::size_t;
#else
#ifdef ORTHOTREE_INDEX_T__UINT_FAST32_T
  using index_t = std::uint_fast32_t;
#else
#ifdef ORTHOTREE_INDEX_T__INT
  using index_t = int;
#else
  using index_t = std::uint32_t;
#endif // ORTHOTREE_INDEX_INT
#endif // ORTHOTREE_INDEX_T__UINT_FAST32_T
#endif // ORTHOTREE_INDEX_T__SIZE_T

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4715)
#endif

  namespace detail
  {
    template<typename TContainer, typename TKey>
    concept HasAt = requires(TContainer container, TKey key) { container.at(key); };

    template<typename T>
    concept HasFirst = requires(T value) { value.first; };
    template<typename T>
    concept HasSecond = requires(T value) { value.second; };

    template<typename, typename key_type = std::void_t<>>
    struct container_key_type
    {
      using type = index_t;
    };

    template<typename TContainer>
    struct container_key_type<TContainer, std::void_t<typename TContainer::key_type>>
    {
      using type = TContainer::key_type;
    };


    template<typename T, std::size_t N>
    constexpr index_t getID(std::array<T, N> const& container, T const& value) noexcept
    {
      return index_t(std::distance(container.data(), &value));
    }

    template<typename T>
    constexpr index_t getID(std::vector<T> const& container, T const& value) noexcept
    {
      return index_t(std::distance(container.data(), &value));
    }


    template<typename T>
    constexpr index_t getID(std::span<T const> const& container, T const& value) noexcept
    {
      return index_t(std::distance(container.data(), &value));
    }

    template<typename TContainer>
    constexpr typename TContainer::key_type getKeyPart(TContainer const&, typename TContainer::value_type const& value) noexcept
      requires(HasFirst<typename TContainer::value_type>)
    {
      return value.first;
    }

    template<typename TContainer>
    constexpr index_t getKeyPart(TContainer const& container, typename TContainer::value_type const& value) noexcept
      requires(std::contiguous_iterator<typename TContainer::iterator>)
    {
      return index_t(std::distance(&container[0], &value));
    }

    template<typename T>
    constexpr const auto& getValuePart(T const& value) noexcept
      requires(HasSecond<T>)
    {
      return value.second;
    }

    template<typename value_type>
    constexpr const auto& getValuePart(value_type const& value) noexcept
    {
      return value;
    }

    template<typename value_type, typename entity_type>
    constexpr void setValuePart(value_type& value, entity_type const& entity) noexcept
      requires(HasSecond<value_type>)
    {
      value.second = entity;
    }

    template<typename value_type, typename entity_type>
    constexpr void setValuePart(value_type& value, entity_type const& entity) noexcept
    {
      value = entity;
    }


    template<typename TContainer, typename TKey>
    constexpr const auto& at(TContainer const& container, TKey const& key) noexcept
      requires(HasAt<TContainer, TKey>)
    {
      return container.at(key);
    }

    template<typename TContainer, typename TKey>
    constexpr auto& at(TContainer& container, TKey const& key) noexcept
      requires(HasAt<TContainer, TKey>)
    {
      return container.at(key);
    }

    template<typename TContainer, typename TKey>
    constexpr const auto& at(TContainer const& continer, TKey const& key) noexcept
    {
      return continer[key];
    }

    template<typename TContainer, typename TKey>
    constexpr auto& at(TContainer& continer, TKey const& key) noexcept
    {
      return continer[key];
    }

    template<typename TContainer, typename TKey, typename TValue>
    constexpr void set(TContainer& container, TKey key, TValue const& value) noexcept
      requires(HasAt<TContainer, TKey>)
    {
      container.at(key) = value;
    }

    template<typename TContainer, typename TKey, typename TValue>
    constexpr void set(TContainer& continer, TKey key, TValue const& value) noexcept
    {
      continer[key] = value;
    }


    struct pair_hash
    {
      template<typename T>
      static constexpr void hash_combine(std::size_t& seed, T value) noexcept
      {
        seed ^= value + std::size_t{ 0x9e3779b9 } + (seed << std::size_t{ 6 }) + (seed >> std::size_t{ 2 });
      }

      template<typename T1, typename T2>
      constexpr std::size_t operator()(std::pair<T1, T2> const& pair) const noexcept
      {
        std::size_t seed = 0;
        hash_combine(seed, pair.first);
        hash_combine(seed, pair.second);
        return seed;
      }
    };

    template<typename TContainer, typename... TElement>
    concept HasEmplaceBack = requires(TContainer container, TElement&&... elements) { container.emplace_back(std::forward<TElement>(elements)...); };

    template<typename TContainer, typename... TElement>
    concept HasEmplace = requires(TContainer container, TElement&&... elements) { container.emplace(std::forward<TElement>(elements)...); };

    template<HasEmplaceBack TContainer, typename... TElement>
    constexpr void emplace(TContainer& container, TElement&&... element) noexcept
    {
      container.emplace_back(std::forward<TElement>(element)...);
    }

    template<HasEmplace TContainer, typename... TElement>
    constexpr void emplace(TContainer& container, TElement&&... element) noexcept
    {
      container.emplace(std::forward<TElement>(element)...);
    }

    template<typename T, bool DOES_ORDER_MATTER>
    static std::pair<T, T> makePair(T a, T b) noexcept
    {
      if constexpr (DOES_ORDER_MATTER)
        return a < b ? std::pair<T, T>{ a, b } : std::pair<T, T>{ b, a };
      else
        return std::pair<T, T>{ a, b };
    }

    template<typename TContainer>
    void sortAndUnique(TContainer& c) noexcept
    {
      std::sort(c.begin(), c.end());
      c.erase(std::unique(c.begin(), c.end()), c.end());
    }

    template<typename TContainer, typename... TElement>
    concept HasReserve = requires(TContainer container) { container.reserve(0); };

    template<HasReserve TContainer>
    inline constexpr void reserve(TContainer& c, std::size_t n) noexcept
    {
      c.reserve(n);
    };

    template<typename TContainer>
    inline constexpr void reserve(TContainer&, std::size_t) noexcept {};

    template<uint8_t e, typename TOut = std::size_t>
    consteval TOut pow2_ce()
    {
      constexpr auto bitSize = sizeof(TOut) * CHAR_BIT;
      static_assert(e >= 0 && e < bitSize);
      return TOut{ 1 } << e;
    }

    template<typename TIn, typename TOut = std::size_t>
    inline constexpr TOut pow2(TIn e) noexcept
    {
      assert(e >= 0 && e < (sizeof(TOut) * CHAR_BIT));
      return TOut{ 1 } << e;
    }

    constexpr void inplaceMerge(auto const& comparator, auto& entityIDs, std::size_t middleIndex) noexcept
    {
      auto const beginIt = entityIDs.begin();
      auto const middleIt = beginIt + middleIndex;
      auto const endIt = entityIDs.end();
      std::sort(middleIt, endIt, comparator);
      std::inplace_merge(beginIt, middleIt, endIt, comparator);
    }

    template<typename T, std::size_t N>
    class inplace_vector
    {
    private:
      using container = std::array<T, N>;

    public:
      using value_type = typename container::value_type;
      using reference = T&;
      using const_reference = const T&;
      using size_type = typename std::size_t;
      using iterator = container::iterator;
      using const_iterator = container::const_iterator;

    public:
      constexpr inplace_vector() = default;

      template<typename... TVals>
      constexpr T& emplace_back(TVals&&... value)
      {
        assert(m_size < N);
        m_stack[m_size] = T(std::forward<TVals>(value)...);
        return m_stack[m_size++];
      }


      constexpr void push_back(const T& value)
      {
        assert(m_size < N);
        m_stack[m_size] = value;
        ++m_size;
      }

      constexpr T& operator[](std::size_t index) { return m_stack[index]; }

      constexpr T const& operator[](std::size_t index) const { return m_stack[index]; }

      constexpr std::size_t size() const { return m_size; }

      constexpr bool empty() const noexcept { return m_size == 0; }

      constexpr iterator insert(iterator whereIt, T const& val)
      {
        assert(m_size < N);
        for (auto it = m_stack.begin() + m_size; it != whereIt; --it)
        {
          *it = std::move(*(it - 1));
        }
        *whereIt = std::move(val);
        ++m_size;
        return whereIt;
      }

      constexpr bool erase(iterator it)
      {
        assert(m_size > 0);

        if (it < begin() || it >= end())
        {
          return false;
        }

        for (auto it2 = it; it2 != end() - 1; ++it2)
        {
          *it2 = std::move(*(it2 + 1));
        }

        --m_size;
        return true;
      }

      constexpr void clear() { m_size = 0; }

      constexpr iterator begin() { return m_stack.begin(); }

      constexpr iterator end() { return m_stack.begin() + m_size; }

      constexpr const_iterator begin() const { return m_stack.begin(); }

      constexpr const_iterator end() const { return m_stack.begin() + m_size; }

    private:
      container m_stack;
      std::size_t m_size = 0;
    };


    template<typename It1, typename It2>
    class proxy_reference
    {
      // reference to a bit within a base word
    private:
      using T1 = typename std::iterator_traits<It1>::value_type;
      using T2 = typename std::iterator_traits<It2>::value_type;
      using R1 = typename std::iterator_traits<It1>::reference;
      using R2 = typename std::iterator_traits<It2>::reference;
      using value_type = std::pair<T1, T2>;
      using reference = std::pair<R1, R2>;

    private:
      constexpr proxy_reference() = default;

    public:
      constexpr proxy_reference(It1 it1, It2 it2) noexcept
      : m_it1(it1)
      , m_it2(it2)
      {}

      constexpr proxy_reference(const proxy_reference&) noexcept = default;
      constexpr proxy_reference(proxy_reference&&) noexcept = default;

      constexpr proxy_reference& operator=(const proxy_reference& right) noexcept
      {
        m_it1 = right.m_it1;
        m_it2 = right.m_it2;
        return *this;
      }

      constexpr proxy_reference& operator=(proxy_reference&& right) noexcept
      {
        *m_it1 = std::move(*right.m_it1);
        *m_it2 = std::move(*right.m_it2);
        return *this;
      }

      constexpr proxy_reference& operator=(const value_type& val) noexcept
      {
        *m_it1 = val.first;
        *m_it2 = val.second;
        return *this;
      }

      constexpr proxy_reference& operator=(value_type&& val) noexcept
      {
        *m_it1 = std::move(val.first);
        *m_it2 = std::move(val.second);
        return *this;
      }

      constexpr operator value_type() const& noexcept { return { *m_it1, *m_it2 }; }
      constexpr operator value_type() && noexcept { return { std::move(*m_it1), std::move(*m_it2) }; }

      constexpr R1 const GetFirst() const noexcept { return *m_it1; }
      constexpr R1 GetFirst() noexcept { return *m_it1; }
      constexpr R2 const GetSecond() const noexcept { return *m_it2; }
      constexpr R2 GetSecond() noexcept { return *m_it2; }

      friend constexpr void swap(proxy_reference left, proxy_reference right) noexcept
      {
        auto val1 = std::move(*left.m_it1);
        *left.m_it1 = std::move(*right.m_it1);
        *right.m_it1 = std::move(val1);

        auto val2 = std::move(*left.m_it2);
        *left.m_it2 = std::move(*right.m_it2);
        *right.m_it2 = std::move(val2);
      }

    private:
      It1 m_it1;
      It2 m_it2;
    };

    template<typename It1, typename It2>
    class zip_iterator
    {
    public:
      using iterator_category = std::random_access_iterator_tag;

      using value_type = std::pair<typename std::iterator_traits<It1>::value_type, typename std::iterator_traits<It2>::value_type>;
      using difference_type = typename std::iterator_traits<It1>::difference_type;
      using pointer = void;
      using reference = proxy_reference<It1, It2>;

      constexpr zip_iterator() noexcept = default;
      constexpr zip_iterator(It1 it1, It2 it2) noexcept
      : it1_(it1)
      , it2_(it2)
      {}

      constexpr reference operator*() const noexcept { return reference(it1_, it2_); }
      constexpr It2 GetSecond() const noexcept { return it2_; }

      constexpr zip_iterator& operator++() noexcept
      {
        ++it1_;
        ++it2_;
        return *this;
      }

      constexpr zip_iterator operator++(int) noexcept
      {
        auto tmp = *this;
        ++(*this);
        return tmp;
      }

      constexpr zip_iterator& operator--() noexcept
      {
        --it1_;
        --it2_;
        return *this;
      }
      constexpr zip_iterator operator--(int) noexcept
      {
        auto tmp = *this;
        --(*this);
        return tmp;
      }

      constexpr zip_iterator& operator+=(difference_type n) noexcept
      {
        it1_ += n;
        it2_ += n;
        return *this;
      }
      constexpr zip_iterator& operator-=(difference_type n) noexcept
      {
        it1_ -= n;
        it2_ -= n;
        return *this;
      }

      constexpr zip_iterator operator+(difference_type n) const noexcept { return zip_iterator(it1_ + n, it2_ + n); }
      constexpr zip_iterator operator-(difference_type n) const noexcept { return zip_iterator(it1_ - n, it2_ - n); }
      constexpr difference_type operator-(const zip_iterator& other) const noexcept { return it1_ - other.it1_; }

      constexpr reference operator[](difference_type n) const noexcept { return *(*this + n); }

      constexpr bool operator==(const zip_iterator& other) const noexcept { return it1_ == other.it1_; }
      constexpr bool operator!=(const zip_iterator& other) const noexcept { return !(*this == other); }
      constexpr bool operator<(const zip_iterator& other) const noexcept { return it1_ < other.it1_; }
      constexpr bool operator>(const zip_iterator& other) const noexcept { return it1_ > other.it1_; }
      constexpr bool operator<=(const zip_iterator& other) const noexcept { return it1_ <= other.it1_; }
      constexpr bool operator>=(const zip_iterator& other) const noexcept { return it1_ >= other.it1_; }

    private:
      It1 it1_;
      It2 it2_;
    };


    template<typename T1, typename T2>
    class zip_view
    {
    public:
      using It1 = typename T1::iterator;
      using It2 = typename T2::iterator;

      using iterator = zip_iterator<It1, It2>;

      constexpr zip_view(T1& data1, T2& data2) noexcept
      : m_data1(data1)
      , m_data2(data2)
      {}

      constexpr iterator begin() const noexcept { return iterator(m_data1.begin(), m_data2.begin()); }
      constexpr iterator end() const noexcept { return iterator(m_data1.end(), m_data2.end()); }

    private:
      T1& m_data1;
      T2& m_data2;
    };


    // MemoryResource is paged-vector style memory handler which allows to make segment deallocation independently from allocation (e.g. middle
    // allocated segment's middle part can be deallocated). Main page is prioritized to reduce heap allocations and being cache efficient.
    template<typename T>
    class MemoryResource
    {
    public:
      using Index = std::uint32_t;
      using PageID = std::uint32_t;

      static constexpr Index INVALID_PAGEID = std::numeric_limits<Index>::max();
      static constexpr Index MAIN_PAGEID = 0;
      static constexpr std::size_t MIN_SEGMENT_SIZE = 4;
      static constexpr std::size_t DEFAULT_PAGE_SIZE = 4096 / sizeof(T);

      using Segment = std::span<T>;
      struct MemorySegment
      {
        PageID pageID = MAIN_PAGEID;
        Segment segment;
      };

    private:
      struct IndexedSegment
      {
        Index begin = 0;
        Index capacity = 0;
      };

    public:
      MemoryResource() = default;
      MemoryResource(MemoryResource const&) = delete;
      MemoryResource(MemoryResource&&) = default;

    public:
      constexpr void Init(std::size_t firstPageSize = DEFAULT_PAGE_SIZE) noexcept
      {
        m_pages.reserve(10);
        m_pages.emplace_back(firstPageSize + MIN_SEGMENT_SIZE);
        m_freeMainSegments.reserve(10);
        m_freeMainSegments.emplace_back(IndexedSegment{ 0, Index(m_pages[0].size()) });
      }

      constexpr void Clone(MemoryResource& resource, std::vector<MemorySegment*> memorySegments) const noexcept
      {
        auto sumsize = std::size_t{};
        for (auto pms : memorySegments)
          sumsize += pms->segment.size();

        resource.m_pages.resize(1);
        auto& page = resource.m_pages[0];
        page.resize(sumsize);

        auto destIt = page.begin();
        for (auto pms : memorySegments)
        {
          auto const size = pms->segment.size();
          if (size == 0)
            continue;

          if constexpr (std::is_trivially_copyable_v<T>)
            std::memcpy(&*destIt, pms->segment.data(), size * sizeof(T));
          else
            std::copy(pms->segment.begin(), pms->segment.end(), destIt);

          pms->pageID = MAIN_PAGEID;
          pms->segment = std::span(destIt, size);

          destIt += size;
        }
      }

      constexpr void Reset() noexcept
      {
        m_pages.clear();
        m_freeMainSegments.clear();
        m_freedPages.clear();
      }

      constexpr void IncreaseSegment(MemorySegment& ms, std::size_t sizeIncrease) noexcept
      {
        if (ms.segment.empty())
        {
          ms = Allocate(sizeIncrease);
          return;
        }

        auto& page = m_pages[ms.pageID];
        if (ms.pageID != MAIN_PAGEID)
        {
          page.resize(page.size() + sizeIncrease);
          ms.segment = std::span(page);
          return;
        }

        auto const sizeIncrease_ = Index(sizeIncrease);
        auto freeSegmentIt = GetConnectingFreeSegment(ms.segment);
        if (freeSegmentIt == m_freeMainSegments.end() || freeSegmentIt->capacity < sizeIncrease_)
        {
          auto newPool = Allocate(ms.segment.size() + sizeIncrease);
          if constexpr (std::is_trivially_copyable_v<T>)
            std::memcpy(newPool.segment.data(), ms.segment.data(), ms.segment.size() * sizeof(T));
          else
            std::copy(ms.segment.begin(), ms.segment.end(), newPool.segment.begin());

          Deallocate(ms);
          ms = newPool;
        }
        else
        {
          auto const begin = GetMainPageIndexOfBegin(ms.segment);
          ms.segment = std::span(page.begin() + begin, ms.segment.size() + 1);
          HandleFreeSegmentChange(freeSegmentIt, freeSegmentIt->begin + sizeIncrease_, freeSegmentIt->capacity - sizeIncrease_);
        }
      }

      constexpr void DecreaseSegment(MemorySegment& ms, std::size_t sizeDecrease)
      {
        if (ms.segment.empty())
          return;

        assert(ms.segment.size() >= sizeDecrease);
        if (ms.pageID == MAIN_PAGEID)
        {
          auto freeMemorySegment = ms;
          freeMemorySegment.segment = ms.segment.last(sizeDecrease);
          Deallocate(freeMemorySegment);
        }
        else
        {
          auto& page = m_pages[ms.pageID];
          page.resize(page.size() - sizeDecrease);
        }

        ms.segment = ms.segment.first(ms.segment.size() - sizeDecrease);
      }

      constexpr MemorySegment Allocate(std::size_t capacity) noexcept
      {
        auto const capacity_ = Index(capacity);

        auto freeSegmentIt = GetFreeSegmentByCapacity(capacity);
        assert(freeSegmentIt == m_freeMainSegments.end() || freeSegmentIt->capacity >= capacity_);

        auto ms = MemorySegment{};
        if (freeSegmentIt != m_freeMainSegments.end())
        {
          ms.pageID = MAIN_PAGEID;
          ms.segment = std::span(m_pages[0].begin() + freeSegmentIt->begin, capacity);
          HandleFreeSegmentChange(freeSegmentIt, freeSegmentIt->begin + capacity_, freeSegmentIt->capacity - capacity_);
        }
        else // new page is required
        {
          if (m_freedPages.empty())
          {
            ms.pageID = Index(m_pages.size());
            m_pages.emplace_back(capacity);
          }
          else
          {
            ms.pageID = Index(m_freedPages.back());
            m_pages[ms.pageID].resize(capacity);
          }
          ms.segment = std::span(m_pages[ms.pageID]);
        }

        return ms;
      }

      constexpr void Deallocate(MemorySegment const& ms) noexcept
      {
        if (ms.segment.empty())
          return;

        if (ms.pageID == MAIN_PAGEID)
        {
          auto nextSegmentIt = GetConnectingFreeSegment(ms.segment);
          auto prevSegmentIt = GetPreviousFreeSegment(ms.segment);
          auto const segmentSize = Index(ms.segment.size());
          if (prevSegmentIt != m_freeMainSegments.end() && nextSegmentIt != m_freeMainSegments.end())
          {
            auto const begin = prevSegmentIt->begin;
            auto const capacity = prevSegmentIt->capacity + segmentSize + nextSegmentIt->capacity;

            // one of the segments will be erased that cause iterator invalidation to the next elements
            if (prevSegmentIt < nextSegmentIt)
            {
              HandleFreeSegmentChange(nextSegmentIt, 0, 0);
              HandleFreeSegmentChange(prevSegmentIt, begin, capacity);
            }
            else
            {
              HandleFreeSegmentChange(prevSegmentIt, 0, 0);
              HandleFreeSegmentChange(nextSegmentIt, begin, capacity);
            }
          }
          else if (prevSegmentIt != m_freeMainSegments.end())
          {
            HandleFreeSegmentChange(prevSegmentIt, prevSegmentIt->begin, prevSegmentIt->capacity + segmentSize);
          }
          else if (nextSegmentIt != m_freeMainSegments.end())
          {
            HandleFreeSegmentChange(nextSegmentIt, nextSegmentIt->begin - segmentSize, nextSegmentIt->capacity + segmentSize);
          }
          else
          {
            if (m_freeMainSegments.empty())
            {
              m_freeMainSegments.push_back({ GetMainPageIndexOfBegin(ms.segment), segmentSize });
#ifdef _DEBUG
              FillWithPattern(m_freeMainSegments.back());
#endif
            }
            else
            {
              auto const& last = m_freeMainSegments.back();
              auto const& newSegment = m_freeMainSegments.emplace_back(IndexedSegment{ GetMainPageIndexOfBegin(ms.segment), last.capacity });
              HandleFreeSegmentChange(m_freeMainSegments.end() - 1, newSegment.begin, segmentSize);
            }
          }
        }
        else
        {
          if (ms.pageID == Index(m_pages.size() - 1))
            m_pages.resize(m_pages.size() - 1);
          else
          {
            m_pages[ms.pageID] = {};
            m_freedPages.emplace_back(ms.pageID);
          }
        }
      }

    private:
      inline constexpr auto GetMainPageIndexOfBegin(Segment const& pool) const noexcept { return Index(&(*pool.begin()) - &m_pages[MAIN_PAGEID][0]); }

      inline constexpr auto GetMainPageIndexOfEnd(Segment const& pool) const noexcept
      {
        return Index(&(*pool.begin()) - &m_pages[MAIN_PAGEID][0] + pool.size());
      }

      constexpr void HandleFreeSegmentChange(auto freeSegmentIt, Index newBegin, Index newCapacity) noexcept
      {
        assert(newCapacity != Index(-1));

        if (newCapacity == 0)
        {
          m_freeMainSegments.erase(freeSegmentIt);
          return;
        }

        auto freeSegmentResultIt = freeSegmentIt;

        // this loops defer the new value assignment, until it finds the final place
        if (newCapacity < freeSegmentIt->capacity)
        {
          for (; freeSegmentIt != m_freeMainSegments.begin() && (freeSegmentIt - 1)->capacity > newCapacity; --freeSegmentIt)
          {
            --freeSegmentResultIt;
            *freeSegmentIt = std::move(*freeSegmentResultIt);
          }
        }
        else
        {
          for (auto const lastFreeSegmentIt = m_freeMainSegments.end() - 1;
               freeSegmentIt != lastFreeSegmentIt && (freeSegmentIt + 1)->capacity < newCapacity;
               ++freeSegmentIt)
          {
            ++freeSegmentResultIt;
            *freeSegmentIt = std::move(*freeSegmentResultIt);
          }
        }

        freeSegmentResultIt->begin = newBegin;
        freeSegmentResultIt->capacity = newCapacity;
#ifdef _DEBUG
        FillWithPattern(*freeSegmentResultIt);
#endif
        assert(CheckFreeSegments());
      }

      constexpr bool CheckFreeSegments() const noexcept
      {
#ifdef _DEBUG
        if (m_freeMainSegments.size() <= 1)
          return true;

        auto segments = m_freeMainSegments;

        std::sort(segments.begin(), segments.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });

        for (size_t i = 1; i < segments.size(); ++i)
        {
          const auto& prev = segments[i - 1];
          const auto& curr = segments[i];
          if (prev.begin + prev.capacity > curr.begin)
          {
            return false;
          }
        }
#endif
        return true;
      }

      constexpr void FillWithPattern(IndexedSegment const& segment) noexcept
      {
        if constexpr (!std::is_integral_v<T>)
          return;

        std::fill(m_pages[0].begin() + segment.begin, m_pages[0].begin() + segment.begin + segment.capacity, std::numeric_limits<T>::max());
      }

      constexpr auto GetFreeSegmentByCapacity(std::size_t capacity) noexcept
      {
        if (m_freeMainSegments.empty() || std::size_t(m_freeMainSegments.back().capacity) < capacity + MIN_SEGMENT_SIZE)
          return m_freeMainSegments.end();

        auto const requiredCapacity = Index(capacity + MIN_SEGMENT_SIZE);
        return std::partition_point(m_freeMainSegments.begin(), m_freeMainSegments.end(), [&](auto const& freeSection) {
          return freeSection.capacity < requiredCapacity;
        });
      }

      constexpr auto GetConnectingFreeSegment(Segment const& allocatedSegment) noexcept
      {
        if (allocatedSegment.empty())
          return m_freeMainSegments.end();

        auto const begin = GetMainPageIndexOfEnd(allocatedSegment);
        if (begin == m_pages[MAIN_PAGEID].size())
          return m_freeMainSegments.end();

        return std::find_if(m_freeMainSegments.begin(), m_freeMainSegments.end(), [begin](auto const& freeSegment) {
          return freeSegment.begin == begin;
        });
      }

      constexpr auto GetPreviousFreeSegment(Segment const& allocatedSegment) noexcept
      {
        if (allocatedSegment.empty())
          return m_freeMainSegments.end();

        auto const begin = GetMainPageIndexOfBegin(allocatedSegment);
        if (begin == 0)
          return m_freeMainSegments.end();

        return std::find_if(m_freeMainSegments.begin(), m_freeMainSegments.end(), [begin](auto const& freeSegment) {
          return freeSegment.begin + freeSegment.capacity == begin;
        });
      }

    private:
      // stores the data
      std::vector<std::vector<T>> m_pages;

      // stores the freed sections of memory in ascending order
      std::vector<IndexedSegment> m_freeMainSegments;

      // stores the freed pages
      std::vector<Index> m_freedPages;
    };
  } // namespace detail

#ifdef _MSC_VER
#pragma warning(default : 4715)
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif


  // Type of the dimension
  using dim_t = uint32_t;

  // Type of depth
  using depth_t = uint32_t;

  // Grid id
  using GridID = uint32_t;

  // Enum of relation with Planes
  enum class PlaneRelation : char
  {
    Negative,
    Hit,
    Positive
  };

  // Adaptor concepts

  template<class TAdapter, typename TVector, typename TBox, typename TRay, typename TPlane, typename TGeometry = double>
  concept AdaptorBasicsConcept = requires(TVector& point, dim_t dimensionID, TGeometry value) {
    { TAdapter::SetPointC(point, dimensionID, value) };
  } && requires(TVector const& point, dim_t dimensionID) {
    { TAdapter::GetPointC(point, dimensionID) } -> std::convertible_to<TGeometry>;
  } && requires(TBox& box, dim_t dimensionID, TGeometry value) {
    { TAdapter::SetBoxMinC(box, dimensionID, value) };
  } && requires(TBox& box, dim_t dimensionID, TGeometry value) {
    { TAdapter::SetBoxMaxC(box, dimensionID, value) };
  } && requires(TBox const& box, dim_t dimensionID) {
    { TAdapter::GetBoxMinC(box, dimensionID) } -> std::convertible_to<TGeometry>;
  } && requires(TBox const& box, dim_t dimensionID) {
    { TAdapter::GetBoxMaxC(box, dimensionID) } -> std::convertible_to<TGeometry>;
  } && requires(TRay const& ray) {
    { TAdapter::GetRayOrigin(ray) } -> std::convertible_to<TVector const&>;
  } && requires(TRay const& ray) {
    { TAdapter::GetRayDirection(ray) } -> std::convertible_to<TVector const&>;
  } && requires(TPlane const& plane) {
    { TAdapter::GetPlaneNormal(plane) } -> std::convertible_to<TVector const&>;
  } && requires(TPlane const& plane) {
    { TAdapter::GetPlaneOrigoDistance(plane) } -> std::convertible_to<TGeometry>;
  };

  template<class TAdapter, typename TVector, typename TBox, typename TRay, typename TPlane, typename TGeometry = double>
  concept AdaptorConcept =
    requires { requires AdaptorBasicsConcept<TAdapter, TVector, TBox, TRay, TPlane, TGeometry>; } && requires(TBox const& box, TVector const& point) {
      { TAdapter::DoesBoxContainPoint(box, point) } -> std::convertible_to<bool>;
    } && requires(TBox const& e1, TBox const& e2, bool e1_must_contain_e2) {
      { TAdapter::AreBoxesOverlapped(e1, e2, e1_must_contain_e2) } -> std::convertible_to<bool>;
    } && requires(TBox const& e1, TBox const& e2) {
      { TAdapter::AreBoxesOverlappedStrict(e1, e2) } -> std::convertible_to<bool>;
    } && requires(TVector const& box, TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance) {
      { TAdapter::GetPointPlaneRelation(box, distanceOfOrigo, planeNormal, tolerance) } -> std::convertible_to<PlaneRelation>;
    } && requires(TBox const& box, TVector const& rayBasePoint, TVector const& rayHeading, TGeometry tolerance) {
      { TAdapter::GetRayBoxDistance(box, rayBasePoint, rayHeading, tolerance) } -> std::convertible_to<std::optional<double>>;
    } && requires(TBox const& box, TRay const& ray, TGeometry tolerance) {
      { TAdapter::GetRayBoxDistance(box, ray, tolerance) } -> std::convertible_to<std::optional<double>>;
    };

  // Adaptors

  // Provides basic accessor and mutator methods for generic geometric types (points, boxes, rays, planes).
  template<dim_t DIMENSION_NO, typename TVector, typename TBox, typename TRay, typename TPlane, typename TGeometry = double>
  struct AdaptorGeneralBasics
  {
    static inline constexpr TGeometry GetPointC(TVector const& point, dim_t dimensionID) noexcept { return point[dimensionID]; }
    static inline constexpr void SetPointC(TVector& point, dim_t dimensionID, TGeometry value) noexcept { point[dimensionID] = value; }

    static inline constexpr TGeometry GetBoxMinC(TBox const& box, dim_t dimensionID) noexcept { return box.Min[dimensionID]; }
    static inline constexpr TGeometry GetBoxMaxC(TBox const& box, dim_t dimensionID) noexcept { return box.Max[dimensionID]; }
    static inline constexpr void SetBoxMinC(TBox& box, dim_t dimensionID, TGeometry value) noexcept { box.Min[dimensionID] = value; }
    static inline constexpr void SetBoxMaxC(TBox& box, dim_t dimensionID, TGeometry value) noexcept { box.Max[dimensionID] = value; }

    static inline constexpr TVector const& GetRayDirection(TRay const& ray) noexcept { return ray.Direction; }
    static inline constexpr TVector const& GetRayOrigin(TRay const& ray) noexcept { return ray.Origin; }

    static inline constexpr TVector const& GetPlaneNormal(TPlane const& plane) noexcept { return plane.Normal; }
    static inline constexpr TGeometry GetPlaneOrigoDistance(TPlane const& plane) noexcept { return plane.OrigoDistance; }
  };

  // Provides general vector/box/ray/plane operations based on a basic adaptor interface. If the geometric types are connected to an BLAS, it is recommended to implement a custom AdaptorGeneral.
  template<dim_t DIMENSION_NO, typename TVector, typename TBox, typename TRay, typename TPlane, typename TGeometry, typename TAdaptorBasics>
  struct AdaptorGeneralBase : TAdaptorBasics
  {
    using Base = TAdaptorBasics;
    static_assert(AdaptorBasicsConcept<Base, TVector, TBox, TRay, TPlane, TGeometry>);

    static constexpr TVector Add(TVector const& ptL, TVector const& ptR) noexcept
    {
      auto point = TVector{};
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        Base::SetPointC(point, dimensionID, Base::GetPointC(ptL, dimensionID) + Base::GetPointC(ptR, dimensionID));

      return point;
    }

    static void MoveBox(TBox& box, TVector const& moveVector) noexcept
    {
      LOOPIVDEP
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
      {
        Base::SetBoxMinC(box, dimensionID, Base::GetBoxMinC(box, dimensionID) + Base::GetPointC(moveVector, dimensionID));
        Base::SetBoxMaxC(box, dimensionID, Base::GetBoxMaxC(box, dimensionID) + Base::GetPointC(moveVector, dimensionID));
      }
    }

    static constexpr TGeometry Size2(TVector const& point) noexcept
    {
      auto d2 = TGeometry{ 0 };
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
      {
        auto const d = Base::GetPointC(point, dimensionID);
        d2 += d * d;
      }
      return d2;
    }

    static constexpr TGeometry Size(TVector const& point) noexcept { return std::sqrt(Size2(point)); }

    static constexpr TGeometry Dot(TVector const& ptL, TVector const& ptR) noexcept
    {
      auto value = TGeometry{};
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        value += Base::GetPointC(ptL, dimensionID) * Base::GetPointC(ptR, dimensionID);

      return value;
    }

    static constexpr TGeometry Distance2(TVector const& ptL, TVector const& ptR) noexcept
    {
      auto d2 = TGeometry{ 0 };
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
      {
        auto const d = Base::GetPointC(ptL, dimensionID) - Base::GetPointC(ptR, dimensionID);
        d2 += d * d;
      }
      return d2;
    }

    static constexpr TGeometry Distance(TVector const& ptL, TVector const& ptR) noexcept { return std::sqrt(Distance2(ptL, ptR)); }

    static constexpr bool ArePointsEqual(TVector const& ptL, TVector const& ptR, TGeometry rAccuracy) noexcept
    {
      return Distance2(ptL, ptR) <= rAccuracy * rAccuracy;
    }

    static constexpr bool IsNormalizedVector(TVector const& normal) noexcept { return std::abs(Size2(normal) - 1.0) < 0.000001; }

    static constexpr bool DoesBoxContainPoint(TBox const& box, TVector const& point, TGeometry tolerance = 0) noexcept
    {
      if (tolerance != 0.0)
      {
        assert(tolerance > 0);
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          if (!(Base::GetBoxMinC(box, dimensionID) - tolerance < Base::GetPointC(point, dimensionID) &&
                Base::GetPointC(point, dimensionID) < Base::GetBoxMaxC(box, dimensionID) + tolerance))
            return false;
      }
      else
      {
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          if (!(Base::GetBoxMinC(box, dimensionID) <= Base::GetPointC(point, dimensionID) &&
                Base::GetPointC(point, dimensionID) <= Base::GetBoxMaxC(box, dimensionID)))
            return false;
      }
      return true;
    }

    enum class EBoxRelation
    {
      Overlapped = -1,
      Adjecent = 0,
      Separated = 1
    };
    static constexpr EBoxRelation GetBoxRelation(TBox const& e1, TBox const& e2) noexcept
    {
      enum EBoxRelationCandidate : uint8_t
      {
        OverlappedC = 0x1,
        AdjecentC = 0x2,
        SeparatedC = 0x4
      };
      uint8_t rel = 0;

      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
      {
        if (Base::GetBoxMinC(e1, dimensionID) < Base::GetBoxMaxC(e2, dimensionID) && Base::GetBoxMaxC(e1, dimensionID) > Base::GetBoxMinC(e2, dimensionID))
          rel |= EBoxRelationCandidate::OverlappedC;
        else if (Base::GetBoxMinC(e1, dimensionID) == Base::GetBoxMaxC(e2, dimensionID) || Base::GetBoxMaxC(e1, dimensionID) == Base::GetBoxMinC(e2, dimensionID))
          rel |= EBoxRelationCandidate::AdjecentC;
        else if (Base::GetBoxMinC(e1, dimensionID) > Base::GetBoxMaxC(e2, dimensionID) || Base::GetBoxMaxC(e1, dimensionID) < Base::GetBoxMinC(e2, dimensionID))
          return EBoxRelation::Separated;
      }

      return (rel & EBoxRelationCandidate::AdjecentC) ? EBoxRelation::Adjecent : EBoxRelation::Overlapped;
    }

    static constexpr bool AreBoxesOverlappedStrict(TBox const& e1, TBox const& e2) noexcept
    {
      return GetBoxRelation(e1, e2) == EBoxRelation::Overlapped;
    }

    static constexpr bool AreBoxesOverlapped(TBox const& e1, TBox const& e2, bool e1_must_contain_e2 = true, bool fOverlapPtTouchAllowed = false) noexcept
    {
      if (e1_must_contain_e2)
      {
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          if (Base::GetBoxMinC(e1, dimensionID) > Base::GetBoxMinC(e2, dimensionID) || Base::GetBoxMinC(e2, dimensionID) > Base::GetBoxMaxC(e1, dimensionID))
            return false;

          if (Base::GetBoxMinC(e1, dimensionID) > Base::GetBoxMaxC(e2, dimensionID) || Base::GetBoxMaxC(e2, dimensionID) > Base::GetBoxMaxC(e1, dimensionID))
            return false;
        }
        return true;
      }
      else
      {
        auto const rel = GetBoxRelation(e1, e2);
        if (fOverlapPtTouchAllowed)
          return rel == EBoxRelation::Adjecent || rel == EBoxRelation::Overlapped;
        else
          return rel == EBoxRelation::Overlapped;
      }
    }

    static constexpr std::optional<double> GetRayBoxDistance(TBox const& box, TVector const& rayOrigin, TVector const& rayDirection, TGeometry tolerance) noexcept
    {
      assert(tolerance >= 0 && "Tolerance cannot be negative!");

      if (DoesBoxContainPoint(box, rayOrigin, tolerance))
        return 0.0;

      auto constexpr inf = std::numeric_limits<double>::max();

      double minBoxDistance = -inf;
      double maxBoxDistance = +inf;
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
      {
        auto const origin = Base::GetPointC(rayOrigin, dimensionID);
        auto const direction = Base::GetPointC(rayDirection, dimensionID);
        auto const boxMin = Base::GetBoxMinC(box, dimensionID) - tolerance;
        auto const boxMax = Base::GetBoxMaxC(box, dimensionID) + tolerance;

        if (direction == 0)
        {
          if (tolerance != 0.0)
          {
            // Box should be within tolerance (<, not <=)
            if (origin <= boxMin || boxMax <= origin)
              return std::nullopt;
          }
          else
          {
            if (origin < boxMin || boxMax < origin)
              return std::nullopt;
          }
        }
        else
        {
          double const directionReciprocal = 1.0 / direction;
          double t1 = (boxMin - origin) * directionReciprocal;
          double t2 = (boxMax - origin) * directionReciprocal;
          if (t1 > t2)
            std::swap(t1, t2);

          minBoxDistance = std::max(minBoxDistance, t1);
          maxBoxDistance = std::min(maxBoxDistance, t2);
        }
      }

      assert(maxBoxDistance != inf && "rayDirection is a zero vector!");
      if (minBoxDistance > maxBoxDistance || maxBoxDistance < 0.0)
        return std::nullopt;
      else
        return minBoxDistance < 0 ? maxBoxDistance : minBoxDistance;
    }

    static constexpr std::optional<double> GetRayBoxDistance(TBox const& box, TRay const& ray, TGeometry tolerance) noexcept
    {
      return GetRayBoxDistance(box, Base::GetRayOrigin(ray), Base::GetRayDirection(ray), tolerance);
    }

    // Get point-Hyperplane relation (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    static constexpr PlaneRelation GetPointPlaneRelation(TVector const& point, TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance) noexcept
    {
      assert(IsNormalizedVector(planeNormal));

      auto const pointProjected = Dot(planeNormal, point);

      if (pointProjected < distanceOfOrigo - tolerance)
        return PlaneRelation::Negative;

      if (pointProjected > distanceOfOrigo + tolerance)
        return PlaneRelation::Positive;

      return PlaneRelation::Hit;
    }
  };


  template<dim_t DIMENSION_NO, typename TVector, typename TBox, typename TRay, typename TPlane, typename TGeometry = double>
  using AdaptorGeneral =
    AdaptorGeneralBase<DIMENSION_NO, TVector, TBox, TRay, TPlane, TGeometry, AdaptorGeneralBasics<DIMENSION_NO, TVector, TBox, TRay, TPlane, TGeometry>>;


  // Bitset helpers for higher dimensions


  template<std::size_t N>
  using bitset_arithmetic = std::bitset<N>;

  template<std::size_t N>
  constexpr auto operator<=>(bitset_arithmetic<N> const& lhs, bitset_arithmetic<N> const& rhs) noexcept
  {
    using R = std::strong_ordering;
    for (std::size_t i = 0, id = N - 1; i < N; ++i, --id)
      if (lhs[id] ^ rhs[id])
        return lhs[id] ? R::greater : R::less;

    return R::equal;
  }

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbitwise-instead-of-logical"
#endif
  template<std::size_t N>
  inline bitset_arithmetic<N> operator+(bitset_arithmetic<N> const& lhs, bitset_arithmetic<N> const& rhs) noexcept
  {
    auto result = bitset_arithmetic<N>();
    bool carry = false;
    for (std::size_t i = 0; i < N; ++i)
    {
      result.set(i, (lhs[i] ^ rhs[i]) ^ carry);
      carry = (lhs[i] & rhs[i]) | (lhs[i] & carry) | (rhs[i] & carry);
    }

    assert(!carry); // unhandled overflow
    return result;
  }

  template<std::size_t N>
  inline constexpr bitset_arithmetic<N> operator-(bitset_arithmetic<N> result, bitset_arithmetic<N> const& rhs) noexcept
  {
    bool borrow = false;
    for (std::size_t index = 0; index < N; ++index)
    {
      bool lhsBit = result[index];
      bool rhsBit = rhs[index];
      result.set(index, lhsBit ^ rhsBit ^ borrow);
      borrow = (!lhsBit & (rhsBit | borrow)) | (lhsBit & rhsBit & borrow);
    }
    return result;
  }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

  template<std::size_t N>
  inline bitset_arithmetic<N> operator+(bitset_arithmetic<N> const& lhs, std::size_t rhs) noexcept
  {
    return lhs + bitset_arithmetic<N>(rhs);
  }

  template<std::size_t N>
  inline bitset_arithmetic<N> operator-(bitset_arithmetic<N> const& lhs, std::size_t rhs) noexcept
  {
    return lhs - bitset_arithmetic<N>(rhs);
  }

  template<std::size_t N>
  inline bitset_arithmetic<N> operator*(bitset_arithmetic<N> const& lhs, bitset_arithmetic<N> const& rhs) noexcept
  {
    auto constexpr mult = [](bitset_arithmetic<N> const& lhs, bitset_arithmetic<N> const& rhs) {
      auto result = bitset_arithmetic<N>{};
      for (std::size_t i = 0; i < N; ++i)
        if (lhs[i])
          result = result + (rhs << i);

      return result;
    };

    return lhs.count() < rhs.count() ? mult(lhs, rhs) : mult(rhs, lhs);
  }

  template<std::size_t N>
  inline bitset_arithmetic<N> operator*(bitset_arithmetic<N> const& lhs, std::size_t rhs) noexcept
  {
    return lhs * bitset_arithmetic<N>(rhs);
  }

  template<std::size_t N>
  inline bitset_arithmetic<N> operator*(std::size_t rhs, bitset_arithmetic<N> const& lhs) noexcept
  {
    return lhs * bitset_arithmetic<N>(rhs);
  }

  struct bitset_arithmetic_compare final
  {
    template<std::size_t N>
    inline constexpr bool operator()(bitset_arithmetic<N> const& lhs, bitset_arithmetic<N> const& rhs) const noexcept
    {
      return lhs < rhs;
    }
  };

  namespace detail
  {
    // Internal geometry system which
    //  - can be instantiated
    //  - is float-based (and not suffer from integer aritmetic issues)
    template<dim_t DIMENSION_NO, typename TGeometry, typename TVector, typename TBox, typename AD>
    struct InternalGeometryModule
    {
      using Geometry = typename std::conditional_t<std::is_integral_v<TGeometry>, float, TGeometry>;
      using Vector = std::array<Geometry, DIMENSION_NO>;
      struct Box
      {
        Vector Min, Max;
      };

      static inline constexpr Geometry Size2(Vector const& vector) noexcept
      {
        auto d2 = Geometry{ 0 };
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          d2 += vector[dimensionID] * vector[dimensionID];

        return d2;
      }

      static inline Geometry Size(Vector const& vector) noexcept { return std::sqrt(Size2(vector)); }

      static inline constexpr Vector GetBoxCenter(Box const& box) noexcept
      {
        Vector center;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          center[dimensionID] = (box.Min[dimensionID] + box.Max[dimensionID]) * Geometry(0.5);

        return center;
      }

      static inline constexpr Vector GetBoxCenterAD(TBox const& box) noexcept
      {
        Vector center;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          center[dimensionID] = (AD::GetBoxMinC(box, dimensionID) + AD::GetBoxMaxC(box, dimensionID)) * Geometry(0.5);

        return center;
      }

      static inline constexpr Vector GetBoxSizeAD(TBox const& box) noexcept
      {
        Vector sizes;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          sizes[dimensionID] = (AD::GetBoxMaxC(box, dimensionID) - AD::GetBoxMinC(box, dimensionID));

        return sizes;
      }

      static inline constexpr Vector GetBoxHalfSizeAD(TBox const& box) noexcept
      {
        Vector halfSize;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          halfSize[dimensionID] = (AD::GetBoxMaxC(box, dimensionID) - AD::GetBoxMinC(box, dimensionID)) * Geometry(0.5);

        return halfSize;
      }

      static inline bool AreBoxesOverlappingByCenter(Vector const& centerLhs, Vector const& centerRhs, Vector const& sizeLhs, Vector const& sizeRhs) noexcept
      {
        Vector distance;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          distance[dimensionID] = centerLhs[dimensionID] - centerRhs[dimensionID];

        Vector sizeLimit;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          sizeLimit[dimensionID] = (sizeLhs[dimensionID] + sizeRhs[dimensionID]) * Geometry(0.5);

        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          if (sizeLimit[dimensionID] <= std::abs(distance[dimensionID]))
            return false;

        return true;
      }

      static inline constexpr void MoveAD(Vector& v, TVector const& moveVector) noexcept
      {
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          v[dimensionID] += AD::GetPointC(moveVector, dimensionID);
      }

      static inline constexpr void MoveAD(Box& box, TVector const& moveVector) noexcept
      {
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          box.Min[dimensionID] += AD::GetPointC(moveVector, dimensionID);
          box.Max[dimensionID] += AD::GetPointC(moveVector, dimensionID);
        }
      }

      static inline constexpr TGeometry DotAD(TVector const& ptL, Vector const& ptR) noexcept
      {
        auto value = TGeometry{};
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          value += AD::GetPointC(ptL, dimensionID) * ptR[dimensionID];

        return value;
      }

      template<typename TGeometryRange, typename TGeometryBox>
      static inline constexpr bool DoesRangeContainBox(TGeometryRange rangeMin, TGeometryRange rangeMax, TGeometryBox boxMin, TGeometryBox boxMax) noexcept
      {
        if (rangeMin > boxMin || boxMin > rangeMax)
          return false;

        if (rangeMin > boxMax || boxMax > rangeMax)
          return false;

        return true;
      }

      static inline constexpr bool DoesRangeContainBoxAD(TBox const& range, Box const& box) noexcept
      {
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          if (!DoesRangeContainBox(AD::GetBoxMinC(range, dimensionID), AD::GetBoxMaxC(range, dimensionID), box.Min[dimensionID], box.Max[dimensionID]))
          {
            return false;
          }
        }
        return true;
      }

      static inline constexpr bool DoesRangeContainBoxAD(Box const& range, TBox const& box) noexcept
      {
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          if (!DoesRangeContainBox(range.Min[dimensionID], range.Max[dimensionID], AD::GetBoxMinC(box, dimensionID), AD::GetBoxMaxC(box, dimensionID)))
          {
            return false;
          }
        }
        return true;
      }

      static inline constexpr bool DoesRangeContainBoxAD(Box const& range, Box const& box) noexcept
      {
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          if (!DoesRangeContainBox(range.Min[dimensionID], range.Max[dimensionID], box.Min[dimensionID], box.Max[dimensionID]))
          {
            return false;
          }
        }
        return true;
      }

      static inline constexpr PlaneRelation GetBoxPlaneRelationAD(
        Vector const& center, Vector const& halfSize, TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance) noexcept
      {
        assert(AD::IsNormalizedVector(planeNormal));

        auto radiusProjected = double(tolerance);
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          radiusProjected += halfSize[dimensionID] * std::abs(AD::GetPointC(planeNormal, dimensionID));

        auto const centerProjected = DotAD(planeNormal, center) - distanceOfOrigo;

        if (centerProjected + radiusProjected < 0.0)
          return PlaneRelation::Negative;

        if (centerProjected - radiusProjected > 0.0)
          return PlaneRelation::Positive;

        return PlaneRelation::Hit;
      }

      static consteval Box BoxInvertedInit() noexcept
      {
        Box ext;
        ext.Min.fill(+std::numeric_limits<Geometry>::max());
        ext.Max.fill(-std::numeric_limits<Geometry>::max());
        return ext;
      }

      static constexpr Box GetBoxAD(TBox const& box) noexcept
      {
        Box boxIGM;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          boxIGM.Min[dimensionID] = Geometry(AD::GetBoxMinC(box, dimensionID));
          boxIGM.Max[dimensionID] = Geometry(AD::GetBoxMaxC(box, dimensionID));
        }

        return boxIGM;
      }

      template<typename TContainer>
      static inline constexpr Box GetBoxOfPointsAD(TContainer const& points) noexcept
      {
        auto ext = BoxInvertedInit();
        for (auto const& e : points)
        {
          auto const& point = detail::getValuePart(e);
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            if (ext.Min[dimensionID] > AD::GetPointC(point, dimensionID))
              ext.Min[dimensionID] = Geometry(AD::GetPointC(point, dimensionID));

            if (ext.Max[dimensionID] < AD::GetPointC(point, dimensionID))
              ext.Max[dimensionID] = Geometry(AD::GetPointC(point, dimensionID));
          }
        }
        return ext;
      }

      template<typename TContainer>
      static inline constexpr Box GetBoxOfBoxesAD(TContainer const& boxes) noexcept
      {
        auto ext = BoxInvertedInit();
        for (auto const& e : boxes)
        {
          auto const& box = detail::getValuePart(e);
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            if (ext.Min[dimensionID] > AD::GetBoxMinC(box, dimensionID))
              ext.Min[dimensionID] = Geometry(AD::GetBoxMinC(box, dimensionID));

            if (ext.Max[dimensionID] < AD::GetBoxMaxC(box, dimensionID))
              ext.Max[dimensionID] = Geometry(AD::GetBoxMaxC(box, dimensionID));
          }
        }
        return ext;
      }

      static inline constexpr bool DoesBoxContainPointAD(Box const& box, TVector const& point, TGeometry tolerance = 0) noexcept
      {
        if (tolerance != 0.0)
        {
          assert(tolerance > 0);
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
            if (!(box.Min[dimensionID] - tolerance < AD::GetPointC(point, dimensionID) &&
                  AD::GetPointC(point, dimensionID) < box.Max[dimensionID] + tolerance))
              return false;
        }
        else
        {
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
            if (!(box.Min[dimensionID] <= AD::GetPointC(point, dimensionID) && AD::GetPointC(point, dimensionID) <= box.Max[dimensionID]))
              return false;
        }
        return true;
      }

      static inline constexpr bool DoesBoxContainPointAD(Vector const& center, Vector const& halfSizes, TVector const& point, TGeometry tolerance = 0) noexcept
      {
        if (tolerance != 0.0)
        {
          assert(tolerance > 0);
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            auto const pointDistance = std::abs(Geometry(AD::GetPointC(point, dimensionID)) - center[dimensionID]);
            auto const halfSize = halfSizes[dimensionID] + tolerance;
            if (pointDistance >= halfSize)
              return false;
          }
        }
        else
        {
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            auto const pointDistance = std::abs(Geometry(AD::GetPointC(point, dimensionID)) - center[dimensionID]);
            if (pointDistance > halfSizes[dimensionID])
              return false;
          }
        }
        return true;
      }

      static Geometry GetBoxWallDistanceAD(TVector const& searchPoint, Vector const& centerPoint, Vector const& halfSize, bool isInsideConsideredAsZero) noexcept
      {
        Vector centerDistance;
        bool isInside = true;
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          centerDistance[dimensionID] = std::abs(centerPoint[dimensionID] - Geometry(AD::GetPointC(searchPoint, dimensionID)));
          isInside &= centerDistance[dimensionID] <= halfSize[dimensionID];
        }

        if (isInside)
        {
          if (isInsideConsideredAsZero)
            return Geometry{};

          auto minWallDistance = halfSize[0];
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            auto const wallDistance = halfSize[dimensionID] - centerDistance[dimensionID];
            if (minWallDistance > wallDistance)
              minWallDistance = wallDistance;
          }
          return minWallDistance;
        }
        else
        {
          Vector distance;
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            distance[dimensionID] = std::max(Geometry{}, centerDistance[dimensionID] - halfSize[dimensionID]);
          }
          return Size(distance);
        }
      }

      static inline constexpr std::optional<Geometry> GetRayBoxDistanceAD(
        Vector const& center, Vector const& halfSizes, TVector const& rayOrigin, TVector const& rayDirection, TGeometry tolerance) noexcept
      {
        assert(tolerance >= 0 && "Tolerance cannot be negative!");
        if (DoesBoxContainPointAD(center, halfSizes, rayOrigin, tolerance))
          return Geometry{};

        auto constexpr inf = std::numeric_limits<Geometry>::max();
        auto minBoxDistance = -inf;
        auto maxBoxDistance = +inf;
        auto const tolerance_ = Geometry(tolerance);

        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          auto const origin = Geometry(AD::GetPointC(rayOrigin, dimensionID));
          auto const direction = Geometry(AD::GetPointC(rayDirection, dimensionID));
          auto const boxMin = center[dimensionID] - halfSizes[dimensionID] - tolerance_;
          auto const boxMax = center[dimensionID] + halfSizes[dimensionID] + tolerance_;
          if (direction == 0)
          {
            if (tolerance != 0.0)
            {
              // Box should be within tolerance (<, not <=)
              if (origin <= boxMin || boxMax <= origin)
                return std::nullopt;
            }
            else
            {
              if (origin < boxMin || boxMax < origin)
                return std::nullopt;
            }
          }
          else
          {
            auto const directionReciprocal = Geometry(1) / direction;
            auto t1 = (boxMin - origin) * directionReciprocal;
            auto t2 = (boxMax - origin) * directionReciprocal;
            if (t1 > t2)
              std::swap(t1, t2);

            minBoxDistance = std::max(minBoxDistance, t1);
            maxBoxDistance = std::min(maxBoxDistance, t2);
          }
        }

        assert(maxBoxDistance != inf && "rayDirection is a zero vector!");
        if (minBoxDistance > maxBoxDistance || maxBoxDistance < 0.0)
          return std::nullopt;
        else
          return minBoxDistance < 0 ? maxBoxDistance : minBoxDistance;
      }

      static inline constexpr Geometry GetVolumeAD(Box const& range) noexcept
      {
        Geometry volume = 1.0;
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          volume *= range.Max[dimensionID] - range.Min[dimensionID];
        }
        return volume;
      }

      static inline constexpr Geometry GetVolumeAD(TBox const& range) noexcept
      {
        Geometry volume = 1.0;
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          volume *= Geometry(AD::GetBoxMaxC(range, dimensionID) - AD::GetBoxMinC(range, dimensionID));
        }
        return volume;
      }
    };


    template<dim_t DIMENSION_NO, typename TGeometry, typename TVector, typename TBox, typename AD>
    class GridSpaceIndexing
    {
    public:
      template<typename T>
      using DimArray = std::array<T, DIMENSION_NO>;

      using IGM = InternalGeometryModule<DIMENSION_NO, TGeometry, TVector, TBox, AD>;
      using IGM_Geometry = typename IGM::Geometry;

    public:
      inline constexpr GridSpaceIndexing() = default;

      inline constexpr GridSpaceIndexing(depth_t maxDepthID, IGM::Box const& boxSpace) noexcept
      : m_maxRasterResolution(detail::pow2<depth_t, GridID>(maxDepthID))
      , m_maxRasterID(m_maxRasterResolution - 1)
      , m_boxSpace(boxSpace)
      {
        auto const subDivisionNoFactor = IGM_Geometry(m_maxRasterResolution);
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          m_sizeInDimensions[dimensionID] = m_boxSpace.Max[dimensionID] - m_boxSpace.Min[dimensionID];
          auto const isFlat = m_sizeInDimensions[dimensionID] == 0;
          m_rasterizerFactors[dimensionID] = isFlat ? IGM_Geometry(1.0) : (subDivisionNoFactor / m_sizeInDimensions[dimensionID]);
        }

        m_volumeOfOverallSpace = IGM::GetVolumeAD(m_boxSpace);
      }

      inline constexpr IGM::Vector const& GetSizes() const noexcept { return m_sizeInDimensions; }

      inline constexpr IGM::Geometry GetVolume() const noexcept { return m_volumeOfOverallSpace; }

      inline constexpr IGM::Box const& GetBoxSpace() const noexcept { return m_boxSpace; }

      inline constexpr void Move(IGM::Vector const& moveVector) noexcept { IGM::MoveAD(m_boxSpace, moveVector); }

      inline constexpr GridID GetResolution() const noexcept { return m_maxRasterResolution; }

      inline constexpr IGM::Vector CalculateGridCellCenter(DimArray<GridID>&& gridID, depth_t&& centerLevel) const noexcept
      {
        using IGM_Vector = typename IGM::Vector;

        auto const halfGrid = IGM_Geometry(detail::pow2(centerLevel)) * IGM_Geometry(0.5);

        IGM_Vector center;
        LOOPIVDEP
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          center[dimensionID] = (IGM_Geometry(gridID[dimensionID]) + halfGrid) / m_rasterizerFactors[dimensionID] + m_boxSpace.Min[dimensionID];

        return center;
      }

      template<bool HANDLE_OUT_OF_TREE_GEOMETRY = false>
      inline constexpr DimArray<GridID> GetPointGridID(TVector const& point) const noexcept
      {
        auto gridIDs = DimArray<GridID>{};
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          auto pointComponent = IGM_Geometry(AD::GetPointC(point, dimensionID)) - m_boxSpace.Min[dimensionID];
          if constexpr (HANDLE_OUT_OF_TREE_GEOMETRY)
          {
            if (pointComponent < 0.0)
              pointComponent = 0.0;
          }
          else
          {
            assert(pointComponent >= 0.0);
          }

          auto const rasterID = GridID(pointComponent * m_rasterizerFactors[dimensionID]);
          gridIDs[dimensionID] = std::min<GridID>(m_maxRasterID, rasterID);
        }
        return gridIDs;
      }

      inline constexpr std::array<DimArray<GridID>, 2> GetEdgePointGridID(TVector const& point) const noexcept
      {
        auto constexpr minRasterID = IGM_Geometry{};
        auto const maxRasterID = static_cast<IGM_Geometry>(m_maxRasterResolution - 1);

        auto pointMinMaxGridID = std::array<DimArray<GridID>, 2>{};
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          auto const rasterID = std::clamp(
            (IGM_Geometry(AD::GetPointC(point, dimensionID)) - m_boxSpace.Min[dimensionID]) * m_rasterizerFactors[dimensionID], minRasterID, maxRasterID);
          pointMinMaxGridID[0][dimensionID] = pointMinMaxGridID[1][dimensionID] = static_cast<GridID>(rasterID);

          if (0 < pointMinMaxGridID[0][dimensionID] && pointMinMaxGridID[0][dimensionID] < m_maxRasterResolution)
            pointMinMaxGridID[0][dimensionID] -= std::floor(rasterID) == rasterID;
        }
        return pointMinMaxGridID;
      }

      template<bool DO_POINT_LIKE_CLASSIFICATION = false, typename TBox_ = TBox>
      inline constexpr std::array<DimArray<GridID>, 2> GetBoxGridID(TBox_ const& box) const noexcept
      {
        std::array<DimArray<GridID>, 2> gridID;
        constexpr IGM_Geometry zero = IGM_Geometry{};
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        {
          IGM_Geometry boxMin, boxMax;
          if constexpr (std::is_same_v<TBox_, TBox>)
          {
            boxMin = IGM_Geometry(AD::GetBoxMinC(box, dimensionID));
            boxMax = IGM_Geometry(AD::GetBoxMaxC(box, dimensionID));
          }
          else
          {
            boxMin = box.Min[dimensionID];
            boxMax = box.Max[dimensionID];
          }

          assert(boxMin <= boxMax && "Wrong bounding box. Input error.");
          auto const minComponentRasterID = (boxMin - m_boxSpace.Min[dimensionID]) * m_rasterizerFactors[dimensionID];
          auto const maxComponentRasterID = (boxMax - m_boxSpace.Min[dimensionID]) * m_rasterizerFactors[dimensionID];

          if constexpr (DO_POINT_LIKE_CLASSIFICATION)
          {
            gridID[0][dimensionID] = std::min(m_maxRasterID, static_cast<GridID>(minComponentRasterID));
            gridID[1][dimensionID] = std::min(m_maxRasterID, static_cast<GridID>(maxComponentRasterID));
          }
          else
          {
            auto const maxRasterID = IGM_Geometry(m_maxRasterResolution);

            gridID[0][dimensionID] = static_cast<GridID>(std::clamp(minComponentRasterID, zero, maxRasterID));
            gridID[1][dimensionID] = static_cast<GridID>(std::clamp(maxComponentRasterID, zero, maxRasterID));

            if ((gridID[0][dimensionID] != gridID[1][dimensionID] && std::floor(maxComponentRasterID) == maxComponentRasterID) || gridID[1][dimensionID] >= m_maxRasterResolution)
            {
              --gridID[1][dimensionID];
            }
          }

          assert(gridID[0][dimensionID] < m_maxRasterResolution);
          assert(gridID[1][dimensionID] < m_maxRasterResolution);
        }
        return gridID;
      }

    private:
      GridID m_maxRasterResolution = {};
      GridID m_maxRasterID = {};

      IGM::Box m_boxSpace = {};
      IGM::Geometry m_volumeOfOverallSpace = {};
      IGM::Vector m_rasterizerFactors = {};
      IGM::Vector m_sizeInDimensions = {};
    };

    template<dim_t DIMENSION_NO>
    struct MortonSpaceIndexing
    {
      static auto constexpr IS_32BIT_LOCATION = DIMENSION_NO < 4;
      static auto constexpr IS_64BIT_LOCATION = !IS_32BIT_LOCATION && DIMENSION_NO < 15;

      // Indexing can be solved with integral types (above this, internal container will be changed to std::map)
      static auto constexpr IS_LINEAR_TREE = IS_32BIT_LOCATION || IS_64BIT_LOCATION;

      static auto constexpr MAX_NONLINEAR_DEPTH_ID = depth_t{ 4 };

      using UnderlyingInt = std::conditional_t<IS_32BIT_LOCATION, uint32_t, uint64_t>;
      using ChildID = UnderlyingInt;

      // Max number of children
      static auto constexpr CHILD_NO = detail::pow2_ce<DIMENSION_NO, ChildID>();

      // Mask for child bit part
      static auto constexpr CHILD_MASK = detail::pow2_ce<DIMENSION_NO, ChildID>() - 1;

      // Max value: 2 ^ nDepth ^ DIMENSION_NO * 2 (signal bit)
      using LinearLocationID = UnderlyingInt;
      using NonLinearLocationID = bitset_arithmetic<DIMENSION_NO * MAX_NONLINEAR_DEPTH_ID + 1>;
      using LocationID = typename std::conditional_t<IS_LINEAR_TREE, LinearLocationID, NonLinearLocationID>;
      using NodeID = LocationID; // same as the LocationID, but depth is signed by a sentinel bit.
      using LocationIDCR = typename std::conditional_t<IS_LINEAR_TREE, LocationID const, LocationID const&>;
      using NodeIDCR = LocationIDCR;
      template<typename T>
      using DimArray = std::array<T, DIMENSION_NO>;

      // Type system determined maximal depth id due to the resolution.
      static auto constexpr MAX_THEORETICAL_DEPTH_ID =
        IS_LINEAR_TREE ? static_cast<depth_t>((CHAR_BIT * sizeof(NodeID) - 1 /*sentinel bit*/)) / DIMENSION_NO : MAX_NONLINEAR_DEPTH_ID;

      struct RangeLocationMetaData
      {
        depth_t DepthID;
        LocationID LocID;
        ChildID TouchedDimensionsFlag;
        ChildID LowerSegmentID;
      };

      class ChildCheckerFixedDepth
      {
      public:
        inline constexpr ChildCheckerFixedDepth(depth_t examinedLevel, LocationIDCR locationID) noexcept
        : m_mask((LocationID(CHILD_MASK)) << (examinedLevel * DIMENSION_NO))
        , m_childFlag(locationID & m_mask)
        {}

        inline ChildID GetChildID(depth_t examinedLevel) const noexcept
        {
          return CastMortonIDToChildID(m_childFlag >> (examinedLevel * DIMENSION_NO));
        }

        inline constexpr bool Test(LocationIDCR locationID) const noexcept { return (locationID & m_mask) == m_childFlag; }

      private:
        LocationID m_mask;
        LocationID m_childFlag;
      };

      class ChildKeyGenerator
      {
      public:
        constexpr ChildKeyGenerator() noexcept = default;
        explicit inline constexpr ChildKeyGenerator(NodeIDCR parentNodeKey) noexcept
        : m_parentFlag(parentNodeKey << DIMENSION_NO)
        {}

        constexpr ChildKeyGenerator(ChildKeyGenerator const&) noexcept = default;
        constexpr ChildKeyGenerator(ChildKeyGenerator&&) noexcept = default;

        inline constexpr NodeID GetChildNodeKey(ChildID childID) const noexcept { return m_parentFlag | NodeID(childID); }

      private:
        NodeID m_parentFlag = {};
      };

      static inline constexpr NodeID GetHashAtDepth(auto&& location, depth_t maxDepthID) noexcept
      {
        return (NodeID{ 1 } << (location.DepthID * DIMENSION_NO)) | (location.LocID >> ((maxDepthID - location.DepthID) * DIMENSION_NO));
      }

      static inline constexpr NodeID GetHash(depth_t depth, LocationIDCR locationID) noexcept
      {
        assert(locationID < (NodeID(1) << (depth * DIMENSION_NO)));
        return (NodeID{ 1 } << (depth * DIMENSION_NO)) | locationID;
      }

      static inline constexpr NodeID GetRootKey() noexcept { return NodeID{ 1 }; }

      static inline constexpr NodeID GetNoneKey() noexcept { return NodeID{ 0 }; }

      static inline constexpr bool IsValidKey(LinearLocationID key) noexcept { return key > 0; }

      static inline bool IsValidKey(NonLinearLocationID const& key) noexcept { return key.any(); }

      static inline constexpr NodeID GetParentKey(NodeIDCR key) noexcept { return key >> DIMENSION_NO; }

      static inline constexpr LocationID GetParentGridID(LocationIDCR locationID) noexcept { return locationID >> DIMENSION_NO; }

      static inline constexpr depth_t GetDepthID(NodeID key) noexcept
      {
        if constexpr (IS_LINEAR_TREE)
        {
          depth_t const usedBitNo = std::bit_width(key) - 1;
          return usedBitNo / DIMENSION_NO;
        }
        else
        {
          for (depth_t d = 0; IsValidKey(key); ++d, key = GetParentKey(key))
            if (key == 1) // If only sentinel bit remains, exit with node depth
              return d;

          CRASH("Bad key! Internal error!"); // Bad key
        }
      }

      static inline constexpr NodeID RemoveSentinelBit(NodeIDCR key, std::optional<depth_t> depthIDOptional = std::nullopt) noexcept
      {
        if constexpr (IS_LINEAR_TREE)
        {
          auto const sentinelBitPosition = std::bit_width(key) - 1;
          return key - (NodeID{ 1 } << sentinelBitPosition);
        }
        else
        {
          auto const depthID = depthIDOptional.has_value() ? depthIDOptional.value() : GetDepthID(key);
          auto const sentinelBitPosition = depthID * DIMENSION_NO;
          return key - (NodeID{ 1 } << sentinelBitPosition);
        }
      }

      static inline constexpr LocationID GetLocationIDOnExaminedLevel(LocationIDCR locationID, depth_t examinationLevel) noexcept
      {
        return locationID >> (examinationLevel * DIMENSION_NO);
      }

      static inline constexpr bool IsAllChildTouched(std::array<LocationID, 2> const& locationIDRange, depth_t examinationLevel) noexcept
      {
        return IsValidKey((locationIDRange[1] - locationIDRange[0]) >> (examinationLevel * DIMENSION_NO - 1));
      }


      static inline constexpr bool IsAllChildTouched(LocationIDCR locationDifference, depth_t examinationLevel) noexcept
      {
        return (CastMortonIDToChildID(locationDifference >> ((examinationLevel - 1) * DIMENSION_NO)) ^ CHILD_MASK) == 0;
      }

      static inline constexpr bool IsAllChildTouched(ChildID touchedDimensionsFlag) noexcept { return touchedDimensionsFlag == CHILD_MASK; }

    private: // Morton aid functions
      // Separates low 16/32 bits of input by 1 bit
      static constexpr LocationID Part1By1(GridID gridID) noexcept
      {
        static_assert(sizeof(GridID) == 4);

        auto locationID = LocationID{ gridID };
        if constexpr (sizeof(LocationID) == 4)
        {
          // 15 bits can be used
          // n = ----------------fedcba9876543210 : Bits initially
          // n = --------fedcba98--------76543210 : After (1)
          // n = ----fedc----ba98----7654----3210 : After (2)
          // n = --fe--dc--ba--98--76--54--32--10 : After (3)
          // n = -f-e-d-c-b-a-9-8-7-6-5-4-3-2-1-0 : After (4)
          locationID = (locationID ^ (locationID << 8)) & LocationID{ 0x00ff00ff }; // (1)
          locationID = (locationID ^ (locationID << 4)) & LocationID{ 0x0f0f0f0f }; // (2)
          locationID = (locationID ^ (locationID << 2)) & LocationID{ 0x33333333 }; // (3)
          locationID = (locationID ^ (locationID << 1)) & LocationID{ 0x55555555 }; // (4)
        }
        else if constexpr (sizeof(LocationID) == 8)
        {
          // 31 bits can be used
          // n = --------------------------------xytsrqponmlkjihgfedcba9876543210 : Bits initially
          // n = ----------------xytsrqponmlkjihg----------------fedcba9876543210 : After (1)
          // n = ----xyts----rqpo----nmlk----jihg----fedc----ba98----7654----3210 : After (2)
          // n = --xy--ts--rq--po--nm--lk--ji--hg--fe--dc--ba--98--76--54--32--10 : After (3)
          // n = -x-y-t-s-r-q-p-o-n-m-l-k-j-i-h-g-f-e-d-c-b-a-9-8-7-6-5-4-3-2-1-0 : After (4)
          locationID = (locationID ^ (locationID << 16)) & LocationID{ 0x0000ffff0000ffff }; // (1)
          locationID = (locationID ^ (locationID << 8)) & LocationID{ 0x00ff00ff00ff00ff };  // (2)
          locationID = (locationID ^ (locationID << 4)) & LocationID{ 0x0f0f0f0f0f0f0f0f };  // (3)
          locationID = (locationID ^ (locationID << 2)) & LocationID{ 0x3333333333333333 };  // (4)
          locationID = (locationID ^ (locationID << 1)) & LocationID{ 0x5555555555555555 };  // (5)
        }
        else
        {
          static_assert(sizeof(LocationID) == 4 || sizeof(LocationID) == 8, "Unsupported LocationID size");
        }

        return locationID;
      }

      // Separates low 16/32 bits of input by 2 bit
      static constexpr LocationID Part1By2(GridID gridID) noexcept
      {
        static_assert(sizeof(GridID) == 4);

        auto locationID = LocationID{ gridID };
        if constexpr (sizeof(LocationID) == 4)
        {
          // 10 bits can be used
          // n = ----------------------9876543210 : Bits initially
          // n = ------98----------------76543210 : After (1)
          // n = ------98--------7654--------3210 : After (2)
          // n = ------98----76----54----32----10 : After (3)
          // n = ----9--8--7--6--5--4--3--2--1--0 : After (4)
          locationID = (locationID ^ (locationID << 16)) & LocationID{ 0xff0000ff }; // (1)
          locationID = (locationID ^ (locationID << 8)) & LocationID{ 0x0300f00f };  // (2)
          locationID = (locationID ^ (locationID << 4)) & LocationID{ 0x030c30c3 };  // (3)
          locationID = (locationID ^ (locationID << 2)) & LocationID{ 0x09249249 };  // (4)
        }
        else if constexpr (sizeof(LocationID) == 8)
        {
          // 21 bits can be used
          // n = -------------------------------------------lkjhgfedcba9876543210 : Bits initially
          // n = -----------lkjhg--------------------------------fedcba9876543210 : After (1)
          // n = -----------lkjhg----------------fedcba98----------------76543210 : After (2)
          // n = ---l--------kjhg--------fedc--------ba98--------7654--------3210 : After (3)
          // n = ---l----kj----hg----fe----dc----ba----98----76----54----32----10 : After (4)
          // n = ---l--k--j--i--h--g--f--e--d--c--b--a--9--7--6--5--4--3--2--1--0 : After (5)
          locationID = (locationID ^ (locationID << 32)) & LocationID{ 0xffff00000000ffff }; // (1)
          locationID = (locationID ^ (locationID << 16)) & LocationID{ 0x00ff0000ff0000ff }; // (2)
          locationID = (locationID ^ (locationID << 8)) & LocationID{ 0xf00f00f00f00f00f };  // (3)
          locationID = (locationID ^ (locationID << 4)) & LocationID{ 0x30c30c30c30c30c3 };  // (4)
          locationID = (locationID ^ (locationID << 2)) & LocationID{ 0x9249249249249249 };  // (5)
        }
        else
        {
          static_assert(sizeof(LocationID) == 4 || sizeof(LocationID) == 8, "Unsupported LocationID size");
        }

        return locationID;
      }

      static consteval LocationID GetBitPattern()
      {
        constexpr auto size = sizeof(LocationID) * CHAR_BIT;
        constexpr auto maxDepth = (size - 1) / DIMENSION_NO;

        auto bitPattern = LocationID{ 0 };
        auto shift = LocationID{ 0 };
        for (dim_t depthID = 0; depthID < maxDepth; ++depthID, shift += DIMENSION_NO)
          bitPattern |= LocationID{ 1 } << shift;

        return bitPattern;
      }

      static consteval std::array<LocationID, DIMENSION_NO> GetBitPatterns()
      {
        constexpr auto bitPattern = GetBitPattern();

        std::array<LocationID, DIMENSION_NO> bitPatterns;
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          bitPatterns[dimensionID] = bitPattern << dimensionID;

        return bitPatterns;
      }

    public:
      static inline LocationID Encode(DimArray<GridID> const& gridID) noexcept
      {
        if constexpr (DIMENSION_NO == 1)
        {
          return LocationID(gridID[0]);
        }
        else if constexpr (DIMENSION_NO == 2)
        {
#ifdef BMI2_PDEP_AVAILABLE
          return _pdep_u32(gridID[1], 0b10101010'10101010'10101010'10101010) | _pdep_u32(gridID[0], 0b01010101'01010101'01010101'01010101);
#else
          return (Part1By1(gridID[1]) << 1) + Part1By1(gridID[0]);
#endif
        }
        else if constexpr (DIMENSION_NO == 3)
        {
#ifdef BMI2_PDEP_AVAILABLE
          return _pdep_u32(gridID[2], 0b00100100'10010010'01001001'00100100) | _pdep_u32(gridID[1], 0b10010010'01001001'00100100'10010010) |
                 _pdep_u32(gridID[0], 0b01001001'00100100'10010010'01001001);
#else
          return (Part1By2(gridID[2]) << 2) + (Part1By2(gridID[1]) << 1) + Part1By2(gridID[0]);
#endif
        }
#ifdef BMI2_PDEP_AVAILABLE
        else if constexpr (IS_64BIT_LOCATION)
        {
          static constexpr auto bitPatterns = GetBitPatterns();

          auto locationID = LocationID{};
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
            locationID |= _pdep_u64(gridID[dimensionID], bitPatterns[dimensionID]);

          return locationID;
        }
#endif
        else
        {
          auto msb = gridID[0];
          for (dim_t dimensionID = 1; dimensionID < DIMENSION_NO; ++dimensionID)
            msb |= gridID[dimensionID];

          LocationID locationID = 0;
          GridID mask = 1;
          for (dim_t i = 0; msb; mask <<= 1, msb >>= 1, ++i)
          {
            LOOPIVDEP
            for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
            {
              auto const shift = dimensionID + i * DIMENSION_NO;
              if constexpr (IS_LINEAR_TREE)
              {
                locationID |= static_cast<LocationID>(gridID[dimensionID] & mask) << (shift - i);
              }
              else
              {
                locationID.set(shift, gridID[dimensionID] & mask);
              }
            }
          }
          return locationID;
        }
      }

      static DimArray<GridID> Decode(NodeIDCR nodeKey, depth_t maxDepthID) noexcept
      {
        auto const depthID = GetDepthID(nodeKey);
        auto gridID = DimArray<GridID>{};
        if constexpr (DIMENSION_NO == 1)
        {
          auto const examinationLevelID = maxDepthID - depthID;
          gridID[0] = GridID(RemoveSentinelBit(nodeKey) << examinationLevelID);
        }
#ifdef BMI2_PDEP_AVAILABLE
        else if constexpr (IS_LINEAR_TREE)
        {
          static constexpr auto bitPatterns = GetBitPatterns();

          auto const examinationLevelID = maxDepthID - depthID;
          const auto locationID = RemoveSentinelBit(nodeKey) << examinationLevelID * DIMENSION_NO;
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            if constexpr (IS_32BIT_LOCATION)
            {
              gridID[dimensionID] = GridID(_pext_u32(locationID, bitPatterns[dimensionID]));
            }
            else
            {
              gridID[dimensionID] = GridID(_pext_u64(locationID, bitPatterns[dimensionID]));
            }
          }
        }
#endif
        else
        {
          auto constexpr mask = LocationID{ 1 };
          for (depth_t examinationLevelID = maxDepthID - depthID, shift = 0; examinationLevelID < maxDepthID; ++examinationLevelID)
            for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID, ++shift)
            {
              if constexpr (IS_LINEAR_TREE)
              {
                gridID[dimensionID] |= ((nodeKey >> shift) & mask) << examinationLevelID;
              }
              else
              {
                gridID[dimensionID] |= GridID{ nodeKey[shift] } << examinationLevelID;
              }
            }
        }
        return gridID;
      }

      static inline ChildID CastMortonIDToChildID(NonLinearLocationID const& bs) noexcept
      {
        assert(bs <= NonLinearLocationID(std::numeric_limits<ChildID>::max()));
        return bs.to_ullong();
      }

      static inline constexpr ChildID CastMortonIDToChildID(LinearLocationID morton) noexcept { return morton; }

      static inline ChildID GetChildID(NodeIDCR key) noexcept
      {
        if constexpr (IS_LINEAR_TREE)
        {
          auto constexpr childMask = LocationID(CHILD_MASK);
          return CastMortonIDToChildID(key & childMask);
        }
        else
        {
          auto childID = NodeID{};
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          {
            childID.set(dimensionID, key[dimensionID]);
          }

          return CastMortonIDToChildID(childID);
        }
      }

      static inline constexpr ChildID GetChildID(LocationIDCR childNodeKey, depth_t examinationLevelID)
      {
        return GetChildID(childNodeKey >> (DIMENSION_NO * (examinationLevelID - 1)));
      }

      static inline constexpr ChildID GetChildIDByDepth(depth_t parentDepth, depth_t childDepth, LocationIDCR childNodeKey)
      {
        auto const depthDifference = childDepth - parentDepth;
        assert(depthDifference > 0);
        return GetChildID(childNodeKey, depthDifference);
      }

      static inline constexpr bool IsChildInGreaterSegment(ChildID childID, dim_t dimensionID) noexcept
      {
        return (ChildID{ 1 } << dimensionID) & childID;
      }

      static inline constexpr bool IsChildInGreaterSegment(NonLinearLocationID const& locationID, dim_t dimensionID) noexcept
      {
        return locationID[dimensionID];
      }

      static inline constexpr std::array<LocationID, 2> GetRangeLocationID(std::array<DimArray<GridID>, 2> const& gridIDRange) noexcept
      {
        return { Encode(gridIDRange[0]), Encode(gridIDRange[1]) };
      }

      static inline constexpr RangeLocationMetaData GetRangeLocationMetaData(depth_t maxDepthID, std::array<LocationID, 2> const& locationIDRange) noexcept
      {
        auto dl = RangeLocationMetaData{ maxDepthID, locationIDRange[0], {}, {} };
        if (locationIDRange[0] != locationIDRange[1])
        {
          auto const locationDifference = locationIDRange[0] ^ locationIDRange[1];
          depth_t levelID = 0;
          if constexpr (IS_LINEAR_TREE)
          {
            auto const differentBitNo = std::bit_width(locationDifference);
            levelID = (differentBitNo + DIMENSION_NO - 1) / DIMENSION_NO;
          }
          else
          {
            for (auto diffLocationFlag = locationDifference; diffLocationFlag != 0; diffLocationFlag >>= DIMENSION_NO)
              ++levelID;
          }

          if (levelID > 0)
          {
            auto constexpr CHILD_MASK_LOC = LocationID{ CHILD_MASK };
            auto const shiftToChildSegment = std::size_t((levelID - 1) * DIMENSION_NO);
            dl.TouchedDimensionsFlag = CastMortonIDToChildID((locationDifference >> shiftToChildSegment) & CHILD_MASK_LOC);
            dl.LocID >>= shiftToChildSegment;
            dl.LowerSegmentID = CastMortonIDToChildID(dl.LocID & CHILD_MASK_LOC);
            dl.LocID >>= DIMENSION_NO;
            dl.LocID <<= (shiftToChildSegment + DIMENSION_NO);
            dl.DepthID -= levelID;
          }
        }

        assert(dl.DepthID <= MAX_THEORETICAL_DEPTH_ID);
        return dl;
      }

      static inline constexpr RangeLocationMetaData GetRangeLocationMetaData(depth_t maxDepthID, std::array<DimArray<GridID>, 2> const& gridIDRange) noexcept
      {
        return GetRangeLocationMetaData(maxDepthID, GetRangeLocationID(gridIDRange));
      }

      static inline constexpr NodeID GetNodeID(depth_t maxDepthID, std::array<DimArray<GridID>, 2> const& gridIDRange) noexcept
      {
        return GetHash(GetRangeLocationMetaData(maxDepthID, gridIDRange));
      }

      static inline constexpr NodeID GetNodeID(depth_t maxDepthID, std::array<LocationID, 2> const& locationIDRange) noexcept
      {
        return GetHashAtDepth(GetRangeLocationMetaData(maxDepthID, locationIDRange), maxDepthID);
      }

      static inline constexpr auto IsLess(RangeLocationMetaData const& leftLocation, RangeLocationMetaData const& rightLocation) noexcept
      {
        return (leftLocation.LocID < rightLocation.LocID) ||
               ((leftLocation.LocID == rightLocation.LocID) && (leftLocation.DepthID < rightLocation.DepthID));
      }
    };


  } // namespace detail

  static constexpr std::size_t DEFAULT_MAX_ELEMENT_IN_NODES = 20;

  // OrthoTrees

  // OrthoTree: Non-owning Base container which spatially organize data ids in N dimension space into a hash-table by Morton Z order.
  template<
    dim_t DIMENSION_NO,
    typename TEntity_,
    typename TVector_,
    typename TBox_,
    typename TRay_,
    typename TPlane_,
    typename TGeometry_ = double,
    typename TAdapter = AdaptorGeneral<DIMENSION_NO, TVector_, TBox_, TRay_, TPlane_, TGeometry_>,
    typename TContainer_ = std::span<TEntity_ const>>
  class OrthoTreeBase
  {
  public:
    static auto constexpr IS_BOX_TYPE = std::is_same_v<TEntity_, TBox_>;
    static auto constexpr IS_CONTIGOUS_CONTAINER = std::contiguous_iterator<typename TContainer_::iterator>;

    using TGeometry = TGeometry_;
    using TVector = TVector_;
    using TBox = TBox_;
    using TRay = TRay_;
    using TPlane = TPlane_;
    using TContainer = TContainer_;
    using TEntityID = detail::container_key_type<TContainer>::type;
    using TEntity = TEntity_;

    using AD = TAdapter;
    static_assert(AdaptorConcept<AD, TVector, TBox, TRay, TPlane, TGeometry>);
    static_assert(0 < DIMENSION_NO && DIMENSION_NO < 64);

    template<typename T>
    using DimArray = std::array<T, DIMENSION_NO>;
    using IGM = typename detail::InternalGeometryModule<DIMENSION_NO, TGeometry, TVector, TBox, TAdapter>;
    using IGM_Geometry = typename IGM::Geometry;

    using SI = detail::MortonSpaceIndexing<DIMENSION_NO>;
    using MortonNodeID = typename SI::NodeID;
    using MortonNodeIDCR = typename SI::NodeIDCR;
    using MortonLocationID = typename SI::LocationID;
    using MortonLocationIDCR = typename SI::LocationIDCR;
    using MortonChildID = typename SI::ChildID;

  public:
    class Node
    {
    private:
      class ChildBitIterator
      {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = MortonLocationID;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        constexpr ChildBitIterator() noexcept = default;
        constexpr ChildBitIterator(MortonNodeIDCR nodeKey, MortonChildID childBits) noexcept
        : m_keyGenerator(nodeKey)
        , m_remainingChildBits(childBits)
        {}


        constexpr MortonNodeID operator*() const noexcept
        {
          auto const childID = std::countr_zero(m_remainingChildBits);
          return m_keyGenerator.GetChildNodeKey(childID);
        }

        constexpr ChildBitIterator& operator++() noexcept
        {
          m_remainingChildBits &= (m_remainingChildBits - 1); // eliminate the least significant one
          return *this;
        }

        constexpr bool operator==(const ChildBitIterator& other) const noexcept { return m_remainingChildBits == other.m_remainingChildBits; }
        constexpr bool operator!=(const ChildBitIterator& other) const noexcept { return m_remainingChildBits != other.m_remainingChildBits; }

      private:
        SI::ChildKeyGenerator m_keyGenerator;
        MortonChildID m_remainingChildBits = {};
      };

      class ChildBitView
      {
      public:
        constexpr ChildBitView(MortonNodeIDCR nodeKey, MortonChildID childBits) noexcept
        : m_nodeKey(nodeKey)
        , m_childBits(childBits)
        {}

        constexpr ChildBitIterator begin() const noexcept { return ChildBitIterator(m_nodeKey, m_childBits); }
        constexpr ChildBitIterator end() const noexcept { return ChildBitIterator(m_nodeKey, {}); }
        constexpr std::size_t size() const noexcept { return std::popcount(m_childBits); }
        constexpr std::size_t empty() const noexcept { return m_childBits == 0; }

      private:
        MortonLocationID m_nodeKey;
        MortonChildID m_childBits;
      };


      class ChildVectorIterator
      {
      private:
        using ContainerIterator = typename std::vector<MortonChildID>::const_iterator;

      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = MortonLocationID;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;


        constexpr ChildVectorIterator() noexcept = default;
        constexpr ChildVectorIterator(MortonNodeIDCR nodeKey, ContainerIterator it) noexcept
        : m_keyGenerator(nodeKey)
        , m_it(it)
        {}

        constexpr ChildVectorIterator(ChildVectorIterator const&) noexcept = default;
        constexpr ChildVectorIterator(ChildVectorIterator&&) noexcept = default;

        constexpr MortonNodeID operator*() const noexcept { return m_keyGenerator.GetChildNodeKey(*m_it); }

        constexpr ChildVectorIterator& operator++() noexcept
        {
          ++m_it;
          return *this;
        }

        constexpr bool operator==(const ChildVectorIterator& other) const noexcept { return m_it == other.m_it; }
        constexpr bool operator!=(const ChildVectorIterator& other) const noexcept { return m_it != other.m_it; }

      private:
        SI::ChildKeyGenerator m_keyGenerator;
        ContainerIterator m_it;
      };

      class ChildVectorView
      {
      public:
        constexpr ChildVectorView(MortonNodeIDCR nodeKey, std::vector<MortonChildID> const& children) noexcept
        : m_nodeKey(nodeKey)
        , m_children(children)
        {}

        constexpr ChildVectorIterator begin() const noexcept { return ChildVectorIterator(m_nodeKey, m_children.begin()); }
        constexpr ChildVectorIterator end() const noexcept { return ChildVectorIterator(m_nodeKey, m_children.end()); }
        constexpr std::size_t size() const noexcept { return m_children.size(); }
        constexpr std::size_t empty() const noexcept { return m_children.empty(); }

      private:
        MortonLocationID m_nodeKey;
        std::vector<MortonChildID> const& m_children;
      };

      static constexpr bool IS_BIT_CHILDCONTAINER = SI::CHILD_NO <= sizeof(MortonChildID) * 8;

    public:
      using ChildContainer = std::conditional_t<IS_BIT_CHILDCONTAINER, MortonChildID, typename std::vector<MortonChildID>>;
      using ChildContainerView = std::conditional_t<IS_BIT_CHILDCONTAINER, ChildBitView, ChildVectorView>;
      using EntityContainer = detail::MemoryResource<TEntityID>::MemorySegment;

    private:
      MortonNodeID m_key{};
      ChildContainer m_children{};
      EntityContainer m_entities{};


#ifndef ORTHOTREE__DISABLED_NODECENTER
      IGM::Vector m_center;
#endif
    public:
      explicit constexpr Node() noexcept = default;
      explicit constexpr Node(MortonNodeID key) noexcept
      : m_key(key)
      {}


#ifndef ORTHOTREE__DISABLED_NODECENTER
      constexpr IGM::Vector const& GetCenter() const noexcept { return m_center; }
      constexpr void SetCenter(IGM::Vector&& center) noexcept { m_center = std::move(center); }
#endif // !ORTHOTREE__DISABLED_NODECENTER

      void Clear() noexcept
      {
        m_entities = {};
        m_children = {};
      }

    public: // Entity handling
      inline constexpr auto const& GetEntities() const noexcept { return m_entities.segment; }

      inline constexpr auto& GetEntities() noexcept { return m_entities.segment; }

      inline constexpr std::size_t GetEntitiesSize() const noexcept { return m_entities.segment.size(); }

      inline constexpr bool IsEntitiesEmpty() const noexcept { return m_entities.segment.empty(); }

      inline constexpr bool ContainsEntity(TEntityID entityID) const noexcept
      {
        return std::find(m_entities.segment.begin(), m_entities.segment.end(), entityID) != m_entities.segment.end();
      }

      inline constexpr void ReplaceEntities(std::span<TEntityID> entities) noexcept { m_entities.segment = std::move(entities); }

      inline constexpr bool RemoveEntity(TEntityID entityID) noexcept
      {
        auto const endIteratorAfterRemove = std::remove(m_entities.segment.begin(), m_entities.segment.end(), entityID);
        if (endIteratorAfterRemove == m_entities.segment.end())
          return false; // id was not registered previously.

        return true;
      }

      inline constexpr void DecreaseEntityIDs(TEntityID removedEntityID) noexcept
      {
        for (auto& id : m_entities.segment)
          id -= removedEntityID < id;
      }

      EntityContainer& GetEntitySegment() { return m_entities; }

    public: // Child handling
      inline constexpr MortonNodeID GetKey() const noexcept { return m_key; }
      inline constexpr void SetKey(MortonNodeIDCR key) noexcept { m_key = key; }
      inline constexpr void AddChild(MortonChildID childID) noexcept
      {
        if constexpr (IS_BIT_CHILDCONTAINER)
        {
          assert(((m_children & (MortonChildID{ 1 } << childID)) == 0) && "Child should not be added twice!");
          m_children |= (MortonChildID{ 1 } << childID);
        }
        else
        {
          auto const it = std::lower_bound(m_children.begin(), m_children.end(), childID);
          if (it != m_children.end() && *it == childID)
          {
            assert(false && "Child should not be added twice!");
            return;
          }
          m_children.insert(it, childID);
        }
      }

      inline constexpr bool HasChild(MortonChildID childID) const noexcept
      {
        if constexpr (IS_BIT_CHILDCONTAINER)
        {
          return m_children & (MortonChildID{ 1 } << childID);
        }
        else
        {
          return std::binary_search(m_children.begin(), m_children.end(), childID);
        }
      }

      inline constexpr void RemoveChild(MortonNodeIDCR childKey) noexcept
      {
        auto const childID = SI::GetChildID(childKey);
        if constexpr (IS_BIT_CHILDCONTAINER)
        {
          m_children &= ~(MortonChildID{ 1 } << childID);
        }
        else
        {
          auto const it = std::lower_bound(m_children.begin(), m_children.end(), childID);
          if (it == m_children.end())
            return;

          m_children.erase(it);
        }
      }

      inline constexpr bool IsAnyChildExist() const noexcept
      {
        if constexpr (IS_BIT_CHILDCONTAINER)
          return m_children > 0;
        else
          return !m_children.empty();
      }

      inline constexpr auto GetChildren() const noexcept { return ChildContainerView(m_key, m_children); }
    };

  protected: // Aid struct to partitioning and distance ordering
    struct ItemDistance
    {
      IGM_Geometry Distance;
      auto operator<=>(ItemDistance const& rhs) const = default;
    };

    struct EntityDistance : ItemDistance
    {
      TEntityID EntityID;
      auto operator<=>(EntityDistance const& rhs) const = default;
    };

    struct BoxDistance : ItemDistance
    {
      MortonNodeID NodeKey;
      Node const* NodePtr;
    };

#ifdef IS_PMR_USED
    template<typename TData>
    using LinearNodeContainer = std::pmr::unordered_map<MortonNodeID, TData>;

    template<typename TData>
    using NonLinearNodeContainer = std::pmr::map<MortonNodeID, TData, bitset_arithmetic_compare>;
#else
    template<typename TData>
    using LinearNodeContainer = std::unordered_map<MortonNodeID, TData>;

    template<typename TData>
    using NonLinearNodeContainer = std::map<MortonNodeID, TData, bitset_arithmetic_compare>;
#endif // IS_PMR_USED

    template<typename TData>
    using NodeContainer = typename std::conditional_t<SI::IS_LINEAR_TREE, LinearNodeContainer<TData>, NonLinearNodeContainer<TData>>;

  protected: // Member variables
#ifdef IS_PMR_USED
    std::pmr::unsynchronized_pool_resource m_umrNodes;
    NodeContainer<Node> m_nodes = NodeContainer<Node>(&m_umrNodes);
#else
    NodeContainer<Node> m_nodes;
#endif

    detail::MemoryResource<TEntityID> m_memoryResource;

    std::size_t m_maxElementNo = DEFAULT_MAX_ELEMENT_IN_NODES;
    depth_t m_maxDepthID = {};

    std::vector<typename IGM::Vector> m_nodeSizes;

    detail::GridSpaceIndexing<DIMENSION_NO, TGeometry, TVector, TBox, AD> m_grid;

  protected: // Constructors
    OrthoTreeBase() = default;

    OrthoTreeBase(OrthoTreeBase&&) = default;

    OrthoTreeBase(OrthoTreeBase const& other)
#ifdef IS_PMR_USED
    : m_umrNodes()
    , m_nodes(&m_umrNodes)
#else
    : m_nodes(other.m_nodes)
#endif
    , m_maxElementNo(other.m_maxElementNo)
    , m_maxDepthID(other.m_maxDepthID)
    , m_nodeSizes(other.m_nodeSizes)
    , m_grid(other.m_grid)
    {
#ifdef IS_PMR_USED
      m_nodes = other.m_nodes;
#endif

      auto segments = std::vector<typename detail::MemoryResource<TEntityID>::MemorySegment*>(m_nodes.size());
      int i = 0;
      for (auto& [key, node] : m_nodes)
      {
        segments[i] = &node.GetEntitySegment();
        ++i;
      }
      other.m_memoryResource.Clone(m_memoryResource, segments);
    }

    OrthoTreeBase& operator=(OrthoTreeBase const& other)
    {
      m_maxElementNo = other.m_maxElementNo;
      m_maxDepthID = other.m_maxDepthID;
      m_nodeSizes = other.m_nodeSizes;
      m_grid = other.m_grid;
      m_nodes = other.m_nodes;

      // using MR = detail::MemoryResource<TEntityID>;
      auto segments = std::vector<typename detail::MemoryResource<TEntityID>::MemorySegment*>(m_nodes.size());
      int i = 0;
      for (auto& [key, node] : m_nodes)
      {
        segments[i] = &node.GetEntitySegment();
        ++i;
      }
      other.m_memoryResource.Clone(m_memoryResource, segments);
      return *this;
    }

  public: // Node helpers
    // Get EntityIDs of the node
    inline constexpr auto const& GetNodeEntities(Node const& node) const noexcept { return node.GetEntities(); }

    // Get EntityIDs of the node
    inline constexpr auto const& GetNodeEntities(MortonNodeIDCR nodeKey) const noexcept { return GetNodeEntities(GetNode(nodeKey)); }

    // Get EntityIDs number of the node
    inline constexpr std::size_t GetNodeEntitiesSize(Node const& node) const noexcept { return node.GetEntitiesSize(); }

    // Get EntityIDs number of the node
    inline constexpr std::size_t GetNodeEntitiesSize(MortonNodeIDCR nodeKey) const noexcept { return GetNodeEntitiesSize(GetNode(nodeKey)); }

    // Is the node has any entity
    inline constexpr bool IsNodeEntitiesEmpty(Node const& node) const noexcept { return node.IsEntitiesEmpty(); }

    // Is the node has any entity
    inline constexpr bool IsNodeEntitiesEmpty(MortonNodeIDCR nodeKey) const noexcept { return IsNodeEntitiesEmpty(GetNode(nodeKey)); }

    // Calculate extent by box of the tree and the key of the node
    constexpr IGM::Vector CalculateNodeCenter(MortonNodeIDCR key) const noexcept
    {
      return m_grid.CalculateGridCellCenter(SI::Decode(key, m_maxDepthID), m_maxDepthID - SI::GetDepthID(key));
    }

#ifdef ORTHOTREE__DISABLED_NODECENTER
    constexpr IGM::Vector GetNodeCenter(MortonNodeIDCR key) const noexcept { return CalculateNodeCenter(key); }
#define GetNodeCenterMacro(inst, key, node) inst->GetNodeCenter(key)
#else
    inline IGM::Vector const& GetNodeCenter(MortonNodeIDCR key) const noexcept { return GetNode(key).GetCenter(); }
#define GetNodeCenterMacro(inst, key, node) node.GetCenter()
#endif // ORTHOTREE__DISABLED_NODECENTER

#ifdef ORTHOTREE__DISABLED_NODESIZE
    constexpr IGM::Vector GetNodeSize(depth_t depthID) const noexcept
    {
      auto const depthFactor = IGM_Geometry(1.0) / IGM_Geometry(detail::pow2(depthID));
      auto const& spaceSizes = this->m_grid.GetSizes();
      typename IGM::Vector size;
      LOOPIVDEP
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
        size[dimensionID] = spaceSizes[dimensionID] * depthFactor;

      return size;
    }
#else
    inline constexpr IGM::Vector const& GetNodeSize(depth_t depthID) const noexcept { return this->m_nodeSizes[depthID]; }
#endif // ORTHOTREE__DISABLED_NODESIZE

    inline constexpr decltype(auto) GetNodeSizeByKey(MortonNodeIDCR key) const noexcept { return this->GetNodeSize(SI::GetDepthID(key)); }

    constexpr IGM::Box GetNodeBox(depth_t depthID, IGM::Vector const& center) const noexcept
    {
      auto const& halfSize = this->GetNodeSize(depthID + 1); // +1: half size will be required
      typename IGM::Box box{ .Min = center, .Max = center };

      LOOPIVDEP
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
      {
        box.Min[dimensionID] -= halfSize[dimensionID];
        box.Max[dimensionID] += halfSize[dimensionID];
      }

      return box;
    }

    inline constexpr IGM::Box GetNodeBox(MortonNodeIDCR key) const noexcept
    {
      return this->GetNodeBox(SI::GetDepthID(key), this->GetNodeCenter(key));
    }

  protected:
    inline constexpr void AddNodeEntity(Node& node, TEntityID newEntity) noexcept
    {
      m_memoryResource.IncreaseSegment(node.GetEntitySegment(), 1);
      node.GetEntities().back() = std::move(newEntity);
    }

    inline constexpr bool RemoveNodeEntity(Node& node, TEntityID entity) noexcept
    {
      auto const isRemoved = node.RemoveEntity(entity);
      if (isRemoved)
        m_memoryResource.DecreaseSegment(node.GetEntitySegment(), 1);

      return isRemoved;
    }

    inline constexpr void ResizeNodeEntities(Node& node, std::size_t size) noexcept
    {
      auto& ms = node.GetEntitySegment();
      m_memoryResource.DecreaseSegment(ms, ms.segment.size() - size);
    }

    inline constexpr Node CreateChild([[maybe_unused]] Node const& parentNode, MortonNodeIDCR childKey) const noexcept
    {
#ifdef ORTHOTREE__DISABLED_NODECENTER
      return Node(childKey);
#else
      auto nodeChild = Node(childKey);

      auto const depthID = SI::GetDepthID(childKey);
      auto const& halfSizes = this->GetNodeSize(depthID + 1);
      auto const& parentCenter = parentNode.GetCenter();

      typename IGM::Vector childCenter;
      LOOPIVDEP
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
      {
        auto const isGreater = SI::IsChildInGreaterSegment(childKey, dimensionID);
        auto const sign = IGM_Geometry(isGreater * 2 - 1);
        childCenter[dimensionID] = parentCenter[dimensionID] + sign * halfSizes[dimensionID];
      }

      nodeChild.SetCenter(std::move(childCenter));
      return nodeChild;
#endif // ORTHOTREE__DISABLED_NODECENTER
    }

    template<bool HANDLE_OUT_OF_TREE_GEOMETRY = false>
    inline constexpr MortonLocationID GetLocationID(TVector const& point) const noexcept
    {
      return SI::Encode(this->m_grid.template GetPointGridID<HANDLE_OUT_OF_TREE_GEOMETRY>(point));
    }

    template<bool HANDLE_OUT_OF_TREE_GEOMETRY = false>
    inline constexpr SI::RangeLocationMetaData GetRangeLocationMetaData(TVector const& point) const noexcept
    {
      return { this->m_maxDepthID, this->GetLocationID<HANDLE_OUT_OF_TREE_GEOMETRY>(point), 0, 0 };
    }

    template<bool HANDLE_OUT_OF_TREE_GEOMETRY = false, typename TBoxItem = TBox>
    inline constexpr SI::RangeLocationMetaData GetRangeLocationMetaData(TBoxItem const& box) const noexcept
    {
      return SI::GetRangeLocationMetaData(this->m_maxDepthID, this->m_grid.template GetBoxGridID<HANDLE_OUT_OF_TREE_GEOMETRY, TBoxItem>(box));
    }

    inline constexpr depth_t GetExaminationLevelID(depth_t depth) const { return m_maxDepthID - depth; }

    bool IsEveryEntityUnique() const noexcept
    {
      auto ids = std::vector<TEntityID>();
      ids.reserve(100);
      std::for_each(m_nodes.begin(), m_nodes.end(), [&](auto& node) {
        auto const& entities = this->GetNodeEntities(node.second);
        ids.insert(ids.end(), entities.begin(), entities.end());
      });

      auto const idsSizeBeforeUnique = ids.size();
      detail::sortAndUnique(ids);
      return idsSizeBeforeUnique == ids.size();
    }

    inline constexpr void TraverseSplitChildren(SI::RangeLocationMetaData const& location, auto&& setPermutationNo, auto&& action) const noexcept
    {
      auto const touchedDimensionNo = std::popcount(location.TouchedDimensionsFlag);
      auto const permutationNo = detail::pow2<MortonChildID, std::size_t>(touchedDimensionNo);

      setPermutationNo(permutationNo);
      for (std::size_t permutationID = 0; permutationID < permutationNo; ++permutationID)
      {
        MortonChildID segmentID = 0;
        std::size_t permutationMask = 1;
        for (MortonChildID dimensionMask = 1; dimensionMask <= location.TouchedDimensionsFlag; dimensionMask <<= 1)
        {
          if (location.TouchedDimensionsFlag & dimensionMask)
          {
            if (permutationID & permutationMask)
            {
              segmentID |= dimensionMask;
            }
            permutationMask <<= 1;
          }
        }
        segmentID += location.LowerSegmentID;
        action(permutationID, segmentID);
      }
    }

    std::vector<MortonChildID> GetSplitChildSegments(SI::RangeLocationMetaData const& location)
    {
      auto children = std::vector<MortonChildID>();
      this->TraverseSplitChildren(
        location,
        [&children](std::size_t permutationNo) { children.resize(permutationNo); },
        [&](std::size_t permutationID, MortonChildID segmentID) { children[permutationID] = segmentID; });

      return children;
    }

    void InsertWithRebalancingSplitToChildren(
      MortonNodeIDCR parentNodeKey,
      Node& parentNode,
      depth_t parentDepth,
      SI::RangeLocationMetaData const& newEntityLocation,
      TEntityID newEntityID,
      TContainer const& geometryCollection) noexcept
    {
      assert(parentNodeKey == SI::GetHashAtDepth(newEntityLocation, this->GetMaxDepthID()) && "ParentNodeKey should be the same as the location's node key.");

      auto const childGenerator = typename SI::ChildKeyGenerator(parentNodeKey);
      for (auto const childID : this->GetSplitChildSegments(newEntityLocation))
      {
        auto childNodeKey = childGenerator.GetChildNodeKey(childID);
        if (parentNode.HasChild(childID))
          this->template InsertWithRebalancingBase<false>(childNodeKey, parentDepth + 1, true, newEntityLocation, newEntityID, geometryCollection);
        else
        {
          parentNode.AddChild(childID);
          auto [childNodeIt, _] = this->m_nodes.emplace(childNodeKey, this->CreateChild(parentNode, childNodeKey));
          AddNodeEntity(childNodeIt->second, newEntityID);
        }
      }
    }

    template<bool DO_UNIQUENESS_CHECK_TO_INDICIES>
    bool InsertWithRebalancingBase(
      MortonNodeIDCR parentNodeKey,
      depth_t parentDepth,
      bool doSplit,
      SI::RangeLocationMetaData const& newEntityLocation,
      TEntityID newEntityID,
      TContainer const& geometryCollection) noexcept
    {
      enum class ControlFlow
      {
        InsertInParentNode,
        SplitToChildren,
        ShouldCreateOnlyOneChild,
        FullRebalancing,
      };


      auto const isEntitySplit = doSplit && !SI::IsAllChildTouched(newEntityLocation.TouchedDimensionsFlag);

      // If newEntityNodeKey is not the equal to the parentNodeKey, it is not exists.
      auto const newEntityNodeKey = SI::GetHashAtDepth(newEntityLocation, this->m_maxDepthID);
      auto const shouldInsertInParentNode = newEntityNodeKey == parentNodeKey || (isEntitySplit && newEntityLocation.DepthID < parentDepth);

      auto& parentNode = this->m_nodes.at(parentNodeKey);
      auto const cf = [&] {
        if (parentDepth == this->m_maxDepthID)
          return ControlFlow::InsertInParentNode;
        else if (parentNode.IsAnyChildExist() && isEntitySplit && newEntityLocation.DepthID == parentDepth)
          return ControlFlow::SplitToChildren;
        else if (parentNode.IsAnyChildExist() && !shouldInsertInParentNode) // !shouldInsertInParentNode means the entity's node not exist
          return ControlFlow::ShouldCreateOnlyOneChild; // If possible, entity should be placed in a leaf node, therefore if the parent node is not a leaf, a new child should be created.
        else if (this->GetNodeEntitiesSize(parentNode) + 1 >= this->m_maxElementNo)
          return ControlFlow::FullRebalancing;
        else
          return ControlFlow::InsertInParentNode;
      }();

      switch (cf)
      {
      case ControlFlow::ShouldCreateOnlyOneChild: {
        auto const childGenerator = typename SI::ChildKeyGenerator(parentNodeKey);
        auto const childID = SI::GetChildID(newEntityLocation.LocID, m_maxDepthID - parentDepth);
        assert(childID < SI::CHILD_NO);
        auto const childNodeKey = childGenerator.GetChildNodeKey(childID);

        parentNode.AddChild(childID);
        auto [childNode, _] = this->m_nodes.emplace(childNodeKey, this->CreateChild(parentNode, childNodeKey));
        this->AddNodeEntity(childNode->second, newEntityID);

        break;
      }

      case ControlFlow::FullRebalancing: {
        auto const childGenerator = typename SI::ChildKeyGenerator(parentNodeKey);
        this->AddNodeEntity(parentNode, newEntityID);
        auto& parentEntities = this->GetNodeEntities(parentNode); // Box entities could be stuck in the parent node.
        auto parentEntitiesNo = parentEntities.size();
        for (std::size_t i = 0; i < parentEntitiesNo; ++i)
        {
          auto const entityID = parentEntities[i];
          auto const entityLocation = this->GetRangeLocationMetaData(detail::at(geometryCollection, entityID));
          auto const isLocationSplit = doSplit && !SI::IsAllChildTouched(entityLocation.TouchedDimensionsFlag);
          if (entityLocation.DepthID + isLocationSplit <= parentDepth) // entity is stucked
          {
            continue;
          }
          else if (isLocationSplit && entityLocation.DepthID == parentDepth) // entity should be split, but with the same node
          {
            this->InsertWithRebalancingSplitToChildren(parentNodeKey, parentNode, parentDepth, entityLocation, entityID, geometryCollection);
          }
          else // entity should be moved to a child node
          {
            auto const childID = SI::GetChildID(entityLocation.LocID, this->GetExaminationLevelID(parentDepth));
            assert(childID < SI::CHILD_NO);
            if (parentNode.HasChild(childID))
            {
              // ShouldCreateOnlyOneChild supposes if newEntityNodeKey == parentNodeKey then parentNodeKey does not exist, therefore we need to find
              // the smallest smallestChildNodeKey which may not have the relevant child.
              auto const entityNodeKey = SI::GetHashAtDepth(entityLocation, this->m_maxDepthID);
              auto const& [smallestChildNodeKey, smallestChildDepthID] = this->FindSmallestNodeKeyWithDepth(entityNodeKey);

              this->template InsertWithRebalancingBase<false>(smallestChildNodeKey, smallestChildDepthID, doSplit, entityLocation, entityID, geometryCollection);
            }
            else
            {
              auto const childNodeKey = childGenerator.GetChildNodeKey(childID);
              parentNode.AddChild(childID);
              auto [childNode, _] = this->m_nodes.emplace(childNodeKey, this->CreateChild(parentNode, childNodeKey));
              this->AddNodeEntity(childNode->second, entityID);
            }
          }

          --parentEntitiesNo;
          parentEntities[i] = std::move(parentEntities[parentEntitiesNo]);
          --i;
        }

        this->ResizeNodeEntities(parentNode, parentEntitiesNo);
        break;
      }

      case ControlFlow::SplitToChildren: {
        this->InsertWithRebalancingSplitToChildren(parentNodeKey, parentNode, parentDepth, newEntityLocation, newEntityID, geometryCollection);
        break;
      }

      case ControlFlow::InsertInParentNode: {
        this->AddNodeEntity(parentNode, newEntityID);
        break;
      }
      }

      if constexpr (DO_UNIQUENESS_CHECK_TO_INDICIES)
        assert(this->IsEveryEntityUnique()); // Assert means: index is already added. Wrong input!

      return true;
    }

    template<bool DO_UNIQUENESS_CHECK_TO_INDICIES>
    bool InsertWithoutRebalancingBase(MortonNodeIDCR existingParentNodeKey, MortonNodeIDCR entityNodeKey, TEntityID entityID, bool doInsertToLeaf) noexcept
    {
      if (entityNodeKey == existingParentNodeKey)
      {
        this->AddNodeEntity(detail::at(this->m_nodes, entityNodeKey), entityID);
        if constexpr (DO_UNIQUENESS_CHECK_TO_INDICIES)
          assert(this->IsEveryEntityUnique()); // Assert means: index is already added. Wrong input!
        return true;
      }

      if (doInsertToLeaf)
      {
        auto nonExistingNodeStack = std::stack<MortonNodeID, std::vector<MortonNodeID>>{};
        auto parentNodeKey = entityNodeKey;
        for (; parentNodeKey != existingParentNodeKey; parentNodeKey = SI::GetParentKey(nonExistingNodeStack.top()))
        {
          if (this->m_nodes.contains(parentNodeKey))
            break;

          nonExistingNodeStack.push(parentNodeKey);
        }

        auto parentNodeIt = this->m_nodes.find(parentNodeKey);
        for (; !nonExistingNodeStack.empty(); nonExistingNodeStack.pop())
        {
          MortonNodeIDCR newParentNodeKey = nonExistingNodeStack.top();

          [[maybe_unused]] bool isSuccessful = false;
          parentNodeIt->second.AddChild(SI::GetChildID(newParentNodeKey));
          std::tie(parentNodeIt, isSuccessful) = this->m_nodes.emplace(newParentNodeKey, this->CreateChild(parentNodeIt->second, newParentNodeKey));
          assert(isSuccessful);
        }
        this->AddNodeEntity(parentNodeIt->second, entityID);
      }
      else
      {
        auto& parentNode = detail::at(this->m_nodes, existingParentNodeKey);
        if (parentNode.IsAnyChildExist())
        {
          auto const parentDepth = SI::GetDepthID(existingParentNodeKey);
          auto const childID = SI::GetChildIDByDepth(parentDepth, SI::GetDepthID(entityNodeKey), entityNodeKey);
          auto const childGenerator = typename SI::ChildKeyGenerator(existingParentNodeKey);
          auto const childNodeKey = childGenerator.GetChildNodeKey(childID);

          parentNode.AddChild(childID);
          auto [childNode, _] = this->m_nodes.emplace(childNodeKey, this->CreateChild(parentNode, childNodeKey));
          this->AddNodeEntity(childNode->second, entityID);
        }
        else
          this->AddNodeEntity(parentNode, entityID);
      }

      if constexpr (DO_UNIQUENESS_CHECK_TO_INDICIES)
        assert(this->IsEveryEntityUnique()); // Assert means: index is already added. Wrong input!

      return true;
    }

    void RemoveNodeIfPossible(Node& node) noexcept
    {
      auto const nodeKey = node.GetKey();
      if (nodeKey == SI::GetRootKey())
        return;

      if (node.IsAnyChildExist() || !IsNodeEntitiesEmpty(node))
        return;

      this->m_memoryResource.Deallocate(node.GetEntitySegment());
      auto const parentKey = SI::GetParentKey(nodeKey);
      auto& parentNode = detail::at(this->m_nodes, parentKey);
      parentNode.RemoveChild(nodeKey);
      this->m_nodes.erase(nodeKey);
    }

  public: // Static aid functions
    static constexpr std::size_t EstimateNodeNumber(std::size_t elementNo, depth_t maxDepthID, std::size_t maxElementNo) noexcept
    {
      assert(maxElementNo > 0);
      assert(maxDepthID > 0);

      if (elementNo < 10)
        return 10;

      auto constexpr rMult = 1.5;
      constexpr depth_t bitSize = sizeof(std::size_t) * CHAR_BIT;
      if ((maxDepthID + 1) * DIMENSION_NO < bitSize)
      {
        auto const nMaxChild = detail::pow2(maxDepthID * DIMENSION_NO);
        auto const nElementInNode = elementNo / nMaxChild;
        if (nElementInNode > maxElementNo / 2)
          return nMaxChild;
      }

      auto const nElementInNodeAvg = static_cast<float>(elementNo) / static_cast<float>(maxElementNo);
      auto const nDepthEstimated = std::min(maxDepthID, static_cast<depth_t>(ceil((log2f(nElementInNodeAvg) + 1.0) / static_cast<float>(DIMENSION_NO))));
      if (nDepthEstimated * DIMENSION_NO < 64)
        return static_cast<std::size_t>(1.05 * detail::pow2(nDepthEstimated * std::min<depth_t>(6, DIMENSION_NO)));

      return static_cast<std::size_t>(rMult * nElementInNodeAvg);
    }

    static inline depth_t EstimateMaxDepth(std::size_t elementNo, std::size_t maxElementNo) noexcept
    {
      if (elementNo <= maxElementNo)
        return 2;

      auto const nLeaf = elementNo / maxElementNo;
      // nLeaf = (2^nDepth)^DIMENSION_NO
      return std::clamp(static_cast<depth_t>(std::log2(nLeaf) / static_cast<double>(DIMENSION_NO)), depth_t(2), SI::MAX_THEORETICAL_DEPTH_ID);
    }


  public: // Getters
    inline constexpr auto const& GetNodes() const noexcept { return m_nodes; }
    inline bool HasNode(MortonNodeIDCR key) const noexcept { return m_nodes.contains(key); }
    inline auto const& GetNode(MortonNodeIDCR key) const noexcept { return m_nodes.at(key); }
    inline constexpr auto const& GetBox() const noexcept { return m_grid.GetBoxSpace(); }
    inline constexpr auto GetMaxDepthID() const noexcept { return m_maxDepthID; }
    inline constexpr auto GetDepthNo() const noexcept { return m_maxDepthID + 1; }
    inline constexpr auto GetResolutionMax() const noexcept { return m_grid.GetResolution(); }
    inline constexpr auto GetNodeIDByEntity(TEntityID entityID) const noexcept
    {
      auto const it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](auto const& keyAndValue) { return keyAndValue.second.ContainsEntity(entityID); });

      return it == m_nodes.end() ? MortonNodeID{} : it->first;
    }

  protected:
    // Alternative creation mode (instead of Create), Init then Insert items into leafs one by one. NOT RECOMMENDED.
    constexpr void InitBase(IGM::Box const& boxSpace, depth_t maxDepthID, std::size_t maxElementNo, std::size_t estimatedEntityNo) noexcept
    {
      CRASH_IF(!this->m_nodes.empty(), "To build/setup/create the tree, use the Create() [recommended] or Init() function. If an already built tree is wanted to be reset, use the Reset() function before Init().");
      CRASH_IF(maxDepthID < 1, "maxDepthID must be largar than 0!");
      CRASH_IF(maxDepthID > SI::MAX_THEORETICAL_DEPTH_ID, "maxDepthID is larger than the applicable with the current DIMENSION_NO!");
      CRASH_IF(maxDepthID >= std::numeric_limits<uint8_t>::max(), "maxDepthID is too large.");
      CRASH_IF(maxElementNo == 0, "maxElementNo must be larger than 0. It is allowed max entity number for one node.");
      CRASH_IF(CHAR_BIT * sizeof(GridID) < maxDepthID, "GridID and maxDepthID are not compatible.");

      this->m_grid = detail::GridSpaceIndexing<DIMENSION_NO, TGeometry, TVector, TBox, AD>(maxDepthID, boxSpace);
      this->m_maxDepthID = maxDepthID;
      this->m_maxElementNo = maxElementNo;

      [[maybe_unused]] auto& nodeRoot = this->m_nodes[SI::GetRootKey()];
      nodeRoot.SetKey(SI::GetRootKey());
#ifndef ORTHOTREE__DISABLED_NODECENTER
      nodeRoot.SetCenter(IGM::GetBoxCenter(boxSpace));
#endif // !ORTHOTREE__DISABLED_NODECENTER

      // the 0-based depth size of the tree is m_maxDepthID+1, and a fictive childnode halfsize (+2) could be asked prematurely.
      depth_t constexpr additionalDepth = 3;
      auto const examinedDepthSize = this->m_maxDepthID + additionalDepth;
      this->m_nodeSizes.resize(examinedDepthSize, this->m_grid.GetSizes());
      auto constexpr multiplier = IGM_Geometry(0.5);
      auto factor = multiplier;
      for (depth_t depthID = 1; depthID < examinedDepthSize; ++depthID, factor *= multiplier)
        for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID)
          this->m_nodeSizes[depthID][dimensionID] *= factor;

      this->m_memoryResource.Init(estimatedEntityNo);
    }

  public: // Main service functions
    // Alternative creation mode (instead of Create), Init then Insert items into leafs one by one. NOT RECOMMENDED.
    inline constexpr void Init(
      TBox const& box,
      depth_t maxDepthID,
      std::size_t maxElementNo = 11,
      std::size_t estimatedEntityNo = detail::MemoryResource<TEntityID>::DEFAULT_PAGE_SIZE) noexcept
    {
      this->InitBase(IGM::GetBoxAD(box), maxDepthID, maxElementNo, estimatedEntityNo);
    }

    using FProcedure = std::function<void(MortonNodeIDCR, Node const&)>;
    using FProcedureUnconditional = std::function<void(MortonNodeIDCR, Node const&, bool)>;
    using FSelector = std::function<bool(MortonNodeIDCR, Node const&)>;
    using FSelectorUnconditional = std::function<bool(MortonNodeIDCR, Node const&)>;

    // Visit nodes with special selection and procedure in breadth-first search order
    void VisitNodes(MortonNodeIDCR rootKey, FProcedure const& procedure, FSelector const& selector) const noexcept
    {
      auto nodeIDsToProceed = std::queue<MortonNodeID>();
      for (nodeIDsToProceed.push(rootKey); !nodeIDsToProceed.empty(); nodeIDsToProceed.pop())
      {
        auto const& key = nodeIDsToProceed.front();
        auto const& node = GetNode(key);
        if (!selector(key, node))
          continue;

        procedure(key, node);

        for (auto childKey : node.GetChildren())
          nodeIDsToProceed.push(childKey);
      }
    }


    // Visit nodes with special selection and procedure in breadth-first search order
    inline void VisitNodes(MortonNodeIDCR rootKey, FProcedure const& procedure) const noexcept
    {
      VisitNodes(rootKey, procedure, [](MortonNodeIDCR, Node const&) { return true; });
    }


    // Visit nodes with special selection and procedure and if unconditional selection is fulfilled descendants will not be test with selector
    void VisitNodes(
      MortonNodeIDCR rootKey, FProcedureUnconditional const& procedure, FSelector const& selector, FSelectorUnconditional const& selectorUnconditional) const noexcept
    {
      struct Search
      {
        MortonNodeIDCR Key;
        bool DoAvoidSelectionParent;
      };

      auto nodesToProceed = std::queue<Search>();
      for (nodesToProceed.push({ rootKey, false }); !nodesToProceed.empty(); nodesToProceed.pop())
      {
        auto const& [key, doAvoidSelectionParent] = nodesToProceed.front();

        auto const& node = GetNode(key);
        if (!doAvoidSelectionParent && !selector(key, node))
          continue;

        auto const doAvoidSelection = doAvoidSelectionParent || selectorUnconditional(key, node);
        procedure(key, node, doAvoidSelection);

        for (MortonNodeIDCR childKey : node.GetChildren())
          nodesToProceed.push({ childKey, doAvoidSelection });
      }
    }


    // Visit nodes with special selection and procedure in depth-first search order
    void VisitNodesInDFS(MortonNodeIDCR key, FProcedure const& procedure, FSelector const& selector) const noexcept
    {
      auto const& node = GetNode(key);
      if (!selector(key, node))
        return;

      procedure(key, node);
      for (MortonNodeIDCR childKey : node.GetChildren())
        VisitNodesInDFS(childKey, procedure, selector);
    }


    // Collect all item id, traversing the tree in breadth-first search order
    std::vector<TEntityID> CollectAllEntitiesInBFS(MortonNodeIDCR rootKey = SI::GetRootKey(), bool shouldSortInsideNodes = false) const noexcept
    {
      auto entityIDs = std::vector<TEntityID>();
      entityIDs.reserve(m_nodes.size() * std::max<std::size_t>(2, m_maxElementNo / 2));

      VisitNodes(rootKey, [&](MortonNodeIDCR, auto const& node) {
        auto const& entities = this->GetNodeEntities(node);
        auto const entityIDsSize = entityIDs.size();
        entityIDs.insert(entityIDs.end(), entities.begin(), entities.end());
        if (shouldSortInsideNodes)
          std::sort(entityIDs.begin() + entityIDsSize, entityIDs.end());
      });
      return entityIDs;
    }

  private:
    void CollectAllEntitiesInDFSRecursive(Node const& parentNode, std::vector<TEntityID>& foundEntities, bool shouldSortInsideNodes) const noexcept
    {
      auto const& entities = this->GetNodeEntities(parentNode);
      auto const entityIDsSize = foundEntities.size();
      foundEntities.insert(foundEntities.end(), entities.begin(), entities.end());
      if (shouldSortInsideNodes)
        std::sort(foundEntities.begin() + entityIDsSize, foundEntities.end());

      for (MortonNodeIDCR childKey : parentNode.GetChildren())
        CollectAllEntitiesInDFSRecursive(this->GetNode(childKey), foundEntities, shouldSortInsideNodes);
    }

  public:
    // Collect all entity id, traversing the tree in depth-first search pre-order
    std::vector<TEntityID> CollectAllEntitiesInDFS(MortonNodeIDCR parentKey = SI::GetRootKey(), bool shouldSortInsideNodes = false) const noexcept
    {
      auto entityIDs = std::vector<TEntityID>{};
      CollectAllEntitiesInDFSRecursive(GetNode(parentKey), entityIDs, shouldSortInsideNodes);
      return entityIDs;
    }

    // Update all element which are in the given hash-table.
    template<bool IS_PARALLEL_EXEC = false, bool DO_UNIQUENESS_CHECK_TO_INDICIES = false>
    void UpdateIndexes(std::unordered_map<TEntityID, std::optional<TEntityID>> const& updateMap) noexcept
    {
      auto const updateMapEndIterator = updateMap.end();

      EXEC_POL_DEF(ep);
      std::for_each(EXEC_POL_ADD(ep) m_nodes.begin(), m_nodes.end(), [&](auto& node) {
        auto& entityIDs = node.second.GetEntities();
        auto entityNo = entityIDs.size();
        for (std::size_t i = 0; i < entityNo; ++i)
        {
          auto const it = updateMap.find(entityIDs[i]);
          if (it == updateMapEndIterator)
            continue;

          if (it->second)
            entityIDs[i] = *it->second;
          else
          {
            --entityNo;
            entityIDs[i] = entityIDs[entityNo];
            --i;
          }
        }
        ResizeNodeEntities(node.second, entityNo);
      });

      if constexpr (DO_UNIQUENESS_CHECK_TO_INDICIES)
        assert(IsEveryEntityUnique()); // Assert means: index replacements causes that multiple object has the same id. Wrong input!
    }

    // Reset the tree
    void Reset() noexcept
    {
      m_nodes.clear();
      m_grid = {};
      m_memoryResource.Reset();
    }


    // Remove all elements and ids, except Root
    void Clear() noexcept
    {
      std::erase_if(m_nodes, [](auto const& p) { return p.first != SI::GetRootKey(); });
      detail::at(m_nodes, SI::GetRootKey()).Clear();
    }


    // Move the whole tree with a std::vector of the movement
    template<bool IS_PARALLEL_EXEC = false>
    void Move(TVector const& moveVector) noexcept
    {
#ifndef ORTHOTREE__DISABLED_NODECENTER
      EXEC_POL_DEF(ep); // GCC 11.3
      std::for_each(EXEC_POL_ADD(ep) m_nodes.begin(), m_nodes.end(), [&moveVector](auto& pairKeyNode) {
        auto center = pairKeyNode.second.GetCenter();
        IGM::MoveAD(center, moveVector);
        pairKeyNode.second.SetCenter(std::move(center));
      });
#endif // !ORTHOTREE__DISABLED_NODECENTER
      m_grid.Move(moveVector);
    }

    std::tuple<MortonNodeID, depth_t> FindSmallestNodeKeyWithDepth(MortonNodeID searchKey) const noexcept
    {
      for (depth_t depthID = SI::GetDepthID(searchKey); SI::IsValidKey(searchKey); searchKey = SI::GetParentKey(searchKey), --depthID)
        if (this->m_nodes.contains(searchKey))
          return { searchKey, depthID };

      return {}; // Not found
    }

    MortonNodeID FindSmallestNodeKey(MortonNodeID searchKey) const noexcept
    {
      for (; SI::IsValidKey(searchKey); searchKey = SI::GetParentKey(searchKey))
        if (this->m_nodes.contains(searchKey))
          return searchKey;

      return MortonNodeID{}; // Not found
    }

    // Get Node ID of a point
    template<bool HANDLE_OUT_OF_TREE_GEOMETRY = false>
    MortonNodeID GetNodeID(TVector const& searchPoint) const noexcept
    {
      return SI::GetHash(m_maxDepthID, this->GetLocationID<HANDLE_OUT_OF_TREE_GEOMETRY>(searchPoint));
    }

    // Get Node ID of a box
    template<bool HANDLE_OUT_OF_TREE_GEOMETRY = false>
    MortonNodeID GetNodeID(TBox const& box) const noexcept
    {
      return SI::GetHashAtDepth(this->GetRangeLocationMetaData<HANDLE_OUT_OF_TREE_GEOMETRY>(box), m_maxDepthID);
    }

    // Find smallest node which contains the box
    template<bool HANDLE_OUT_OF_TREE_GEOMETRY = false>
    MortonNodeID FindSmallestNode(TVector const& searchPoint) const noexcept
    {
      if constexpr (!HANDLE_OUT_OF_TREE_GEOMETRY)
      {
        if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), searchPoint))
          return MortonNodeID{};
      }
      return this->FindSmallestNodeKey(this->GetNodeID<HANDLE_OUT_OF_TREE_GEOMETRY>(searchPoint));
    }

    // Find smallest node which contains the box
    MortonNodeID FindSmallestNode(TBox const& box) const noexcept
    {
      if (!IGM::DoesRangeContainBoxAD(this->m_grid.GetBoxSpace(), box))
        return MortonNodeID{};

      return FindSmallestNodeKey(this->GetNodeID(box));
    }

    MortonNodeID Find(TEntityID entityID) const noexcept { return GetNodeIDByEntity(entityID); }

  protected:
    template<bool IS_ENTITY_IN_MULTIPLE_NODE = false, bool DO_UPDATE_ENTITY_IDS = IS_CONTIGOUS_CONTAINER>
    constexpr bool EraseEntityBase(TEntityID entityID) noexcept
    {
      auto erasableNodes = std::vector<MortonNodeID>{};
      bool isErased = false;
      for (auto& [nodeKey, node] : this->m_nodes)
      {
        if (!this->RemoveNodeEntity(node, entityID))
          continue;

        isErased = true;
        if constexpr (IS_ENTITY_IN_MULTIPLE_NODE)
        {
          erasableNodes.emplace_back(nodeKey);
        }
        else
        {
          this->RemoveNodeIfPossible(node);
          break;
        }
      }

      if (!isErased)
        return false;

      if constexpr (IS_ENTITY_IN_MULTIPLE_NODE)
      {
        for (MortonNodeIDCR nodeKey : erasableNodes)
          this->RemoveNodeIfPossible(this->m_nodes.at(nodeKey));
      }

      if constexpr (DO_UPDATE_ENTITY_IDS)
      {
        for (auto& [key, node] : this->m_nodes)
          node.DecreaseEntityIDs(entityID);
      }

      return true;
    }

    template<bool DO_RANGE_MUST_FULLY_CONTAIN = false>
    constexpr void RangeSearchBaseCopy(
      TBox const& range, TContainer const& geometryCollection, Node const& parentNode, std::vector<TEntityID>& foundEntities) const noexcept
    {
      auto const& entityIDs = this->GetNodeEntities(parentNode);
      for (auto const entityID : entityIDs)
      {
        bool isEntityInRange = false;
        if constexpr (IS_BOX_TYPE)
        {
          if constexpr (DO_RANGE_MUST_FULLY_CONTAIN)
            isEntityInRange = AD::AreBoxesOverlapped(range, detail::at(geometryCollection, entityID), DO_RANGE_MUST_FULLY_CONTAIN);
          else
            isEntityInRange = AD::AreBoxesOverlappedStrict(range, detail::at(geometryCollection, entityID));
        }
        else
        {
          isEntityInRange = AD::DoesBoxContainPoint(range, detail::at(geometryCollection, entityID));
        }

        if (isEntityInRange)
          foundEntities.emplace_back(entityID);
      }
    }

    struct OverlappingSpaceSegments
    {
      // Flags to sign the overlapped segments dimension-wise
      MortonLocationID minSegmentFlag{}, maxSegmentFlag{};
    };

    static constexpr OverlappingSpaceSegments GetRelativeMinMaxLocation(IGM::Vector const& center, TBox const& range) noexcept
    {
      auto overlappedSegments = OverlappingSpaceSegments{};
      auto segmentBit = MortonLocationID{ 1 };
      for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID, segmentBit <<= 1)
      {
        overlappedSegments.minSegmentFlag |= segmentBit * (center[dimensionID] <= AD::GetBoxMinC(range, dimensionID));
        overlappedSegments.maxSegmentFlag |= segmentBit * (center[dimensionID] <= AD::GetBoxMaxC(range, dimensionID));
      }
      return overlappedSegments;
    }

    template<bool DO_RANGE_MUST_FULLY_CONTAIN = false>
    void RangeSearchBase(
      TBox const& range, TContainer const& geometryCollection, depth_t depthID, MortonNodeIDCR currentNodeKey, std::vector<TEntityID>& foundEntities) const noexcept
    {
      auto const& currentNode = this->GetNode(currentNodeKey);
      if (!currentNode.IsAnyChildExist())
      {
        RangeSearchBaseCopy<DO_RANGE_MUST_FULLY_CONTAIN>(range, geometryCollection, currentNode, foundEntities);
        return;
      }

      auto const& center = GetNodeCenterMacro(this, currentNodeKey, currentNode);
      auto const [minSegmentFlag, maxSegmentFlag] = GetRelativeMinMaxLocation(center, range);

      // Different min-max bit means: the dimension should be totally walked
      // Same min-max bit means: only the min or max should be walked

      // The key will have signal bit also, dimensionMask is applied to calculate only the last, dimension part of the key
      auto const dimensionMask = MortonLocationID{ SI::CHILD_MASK };

      // Sign the dimensions which should not be walked fully
      auto const limitedDimensionsMask = (~(minSegmentFlag ^ maxSegmentFlag)) & dimensionMask;

      if (limitedDimensionsMask == MortonLocationID{} && IGM::DoesRangeContainBoxAD(range, this->GetNodeBox(depthID, center)))
      {
        CollectAllEntitiesInDFSRecursive(currentNode, foundEntities, false);
        return;
      }

      RangeSearchBaseCopy<DO_RANGE_MUST_FULLY_CONTAIN>(range, geometryCollection, currentNode, foundEntities);

      // Sign which element should be walked in the limited dimensions
      auto const dimensionBoundaries = (minSegmentFlag & maxSegmentFlag) & limitedDimensionsMask;

      ++depthID;
      for (MortonNodeIDCR keyChild : currentNode.GetChildren())
      {
        // keyChild should have the same elements in the limited dimensions
        auto const isOverlapped = (keyChild & limitedDimensionsMask) == dimensionBoundaries;
        if (!isOverlapped)
          continue;

        RangeSearchBase<DO_RANGE_MUST_FULLY_CONTAIN>(range, geometryCollection, depthID, keyChild, foundEntities);
      }
    }

    template<bool DO_RANGE_MUST_FULLY_CONTAIN = false, bool DOES_LEAF_NODE_CONTAIN_ELEMENT_ONLY = true>
    bool RangeSearchBaseRoot(TBox const& range, TContainer const& geometryCollection, std::vector<TEntityID>& foundEntities) const noexcept
    {
      auto const entityNo = geometryCollection.size();
      if (IGM::DoesRangeContainBoxAD(range, this->m_grid.GetBoxSpace()))
      {
        foundEntities.resize(entityNo);

        if constexpr (IS_CONTIGOUS_CONTAINER)
          std::iota(foundEntities.begin(), foundEntities.end(), 0);
        else
          std::transform(geometryCollection.begin(), geometryCollection.end(), foundEntities.begin(), [&geometryCollection](auto const& item) {
            return detail::getKeyPart(geometryCollection, item);
          });

        return entityNo > 0;
      }

      // If the range has zero volume, it could stuck at any node comparison with point/side touch. It is eliminated to work node bounding box independently.
      const auto rangeVolume = IGM::GetVolumeAD(range);
      if (rangeVolume <= 0.0)
      {
        return false;
      }

      auto const rangeKey = this->GetNodeID<!IS_BOX_TYPE>(range);
      auto smallestNodeKey = this->FindSmallestNodeKey(rangeKey);
      if (!SI::IsValidKey(smallestNodeKey))
        return false;

      auto const foundEntityNoEstimation =
        this->m_grid.GetVolume() < 0.01 ? 10 : static_cast<std::size_t>((rangeVolume * entityNo) / this->m_grid.GetVolume());

      foundEntities.reserve(foundEntityNoEstimation);
      RangeSearchBase<DO_RANGE_MUST_FULLY_CONTAIN>(range, geometryCollection, SI::GetDepthID(smallestNodeKey), smallestNodeKey, foundEntities);

      if constexpr (!DOES_LEAF_NODE_CONTAIN_ELEMENT_ONLY)
      {
        for (smallestNodeKey = SI::GetParentKey(smallestNodeKey); SI::IsValidKey(smallestNodeKey); smallestNodeKey = SI::GetParentKey(smallestNodeKey))
          RangeSearchBaseCopy<DO_RANGE_MUST_FULLY_CONTAIN>(range, geometryCollection, this->GetNode(smallestNodeKey), foundEntities);
      }

      return true;
    }

    static PlaneRelation GetEntityPlaneRelation(TEntity const& entity, TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance)
    {
      if constexpr (IS_BOX_TYPE)
        return IGM::GetBoxPlaneRelationAD(IGM::GetBoxCenterAD(entity), IGM::GetBoxHalfSizeAD(entity), distanceOfOrigo, planeNormal, tolerance);
      else
        return AD::GetPointPlaneRelation(entity, distanceOfOrigo, planeNormal, tolerance);
    }

    std::vector<TEntityID> PlaneIntersectionBase(TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance, TContainer const& data) const noexcept
    {
      assert(AD::IsNormalizedVector(planeNormal));

      auto results = std::vector<TEntityID>{};
      auto const selector = [&](MortonNodeIDCR key, [[maybe_unused]] Node const& node) -> bool {
        auto const& halfSize = this->GetNodeSize(SI::GetDepthID(key) + 1);
        return IGM::GetBoxPlaneRelationAD(GetNodeCenterMacro(this, key, node), halfSize, distanceOfOrigo, planeNormal, tolerance) == PlaneRelation::Hit;
      };

      auto const procedure = [&](MortonNodeIDCR, Node const& node) {
        for (auto const entityID : this->GetNodeEntities(node))
          if (GetEntityPlaneRelation(detail::at(data, entityID), distanceOfOrigo, planeNormal, tolerance) == PlaneRelation::Hit)
            if (std::find(results.begin(), results.end(), entityID) == results.end())
              results.emplace_back(entityID);
      };

      this->VisitNodesInDFS(SI::GetRootKey(), procedure, selector);

      return results;
    }

    std::vector<TEntityID> PlanePositiveSegmentationBase(
      TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance, TContainer const& data) const noexcept
    {
      assert(AD::IsNormalizedVector(planeNormal));

      auto results = std::vector<TEntityID>{};
      auto const selector = [&](MortonNodeIDCR key, [[maybe_unused]] Node const& node) -> bool {
        auto const& halfSize = this->GetNodeSize(SI::GetDepthID(key) + 1);
        auto const relation = IGM::GetBoxPlaneRelationAD(GetNodeCenterMacro(this, key, node), halfSize, distanceOfOrigo, planeNormal, tolerance);
        return relation != PlaneRelation::Negative;
      };

      auto const procedure = [&](MortonNodeIDCR, Node const& node) {
        for (auto const entityID : this->GetNodeEntities(node))
        {
          auto const relation = GetEntityPlaneRelation(detail::at(data, entityID), distanceOfOrigo, planeNormal, tolerance);
          if (relation == PlaneRelation::Negative)
            continue;

          if (std::find(results.begin(), results.end(), entityID) == results.end())
            results.emplace_back(entityID);
        }
      };

      this->VisitNodesInDFS(SI::GetRootKey(), procedure, selector);

      return results;
    }

    // Get all entities which relation is positive or intersected by the given space boundary planes
    std::vector<TEntityID> FrustumCullingBase(std::span<TPlane const> const& boundaryPlanes, TGeometry tolerance, TContainer const& data) const noexcept
    {
      auto results = std::vector<TEntityID>{};
      if (boundaryPlanes.empty())
        return results;

      assert(std::all_of(boundaryPlanes.begin(), boundaryPlanes.end(), [](auto const& plane) -> bool {
        return AD::IsNormalizedVector(AD::GetPlaneNormal(plane));
      }));

      auto const selector = [&](MortonNodeIDCR key, [[maybe_unused]] Node const& node) -> bool {
        auto const& halfSize = this->GetNodeSize(SI::GetDepthID(key) + 1);
        auto const& center = GetNodeCenterMacro(this, key, node);

        for (auto const& plane : boundaryPlanes)
        {
          auto const relation = IGM::GetBoxPlaneRelationAD(center, halfSize, AD::GetPlaneOrigoDistance(plane), AD::GetPlaneNormal(plane), tolerance);
          if (relation == PlaneRelation::Hit)
            return true;

          if (relation == PlaneRelation::Negative)
            return false;
        }
        return true;
      };

      auto const procedure = [&](MortonNodeIDCR, Node const& node) {
        for (auto const entityID : this->GetNodeEntities(node))
        {
          auto relation = PlaneRelation::Negative;
          for (auto const& plane : boundaryPlanes)
          {
            relation = GetEntityPlaneRelation(detail::at(data, entityID), AD::GetPlaneOrigoDistance(plane), AD::GetPlaneNormal(plane), tolerance);
            if (relation != PlaneRelation::Positive)
              break;
          }

          if (relation == PlaneRelation::Negative)
            continue;

          if (std::find(results.begin(), results.end(), entityID) == results.end())
            results.emplace_back(entityID);
        }
      };

      this->VisitNodesInDFS(SI::GetRootKey(), procedure, selector);

      return results;
    }
  };

  namespace ExecutionTags
  {
    // Sequential execution tag
    struct Sequential
    {};

    // Parallel execution tag
    struct Parallel
    {};
  } // namespace ExecutionTags

  auto constexpr SEQ_EXEC = ExecutionTags::Sequential{};
  auto constexpr PAR_EXEC = ExecutionTags::Parallel{};

  // OrthoTreePoint: Non-owning container which spatially organize point ids in N dimension space into a hash-table by Morton Z order.
  template<
    dim_t DIMENSION_NO,
    typename TVector_,
    typename TBox_,
    typename TRay_,
    typename TPlane_,
    typename TGeometry_ = double,
    typename TAdapter_ = AdaptorGeneral<DIMENSION_NO, TVector_, TBox_, TRay_, TPlane_, TGeometry_>,
    typename TContainer_ = std::span<TVector_ const>>
  class OrthoTreePoint final : public OrthoTreeBase<DIMENSION_NO, TVector_, TVector_, TBox_, TRay_, TPlane_, TGeometry_, TAdapter_, TContainer_>
  {
  protected:
    using Base = OrthoTreeBase<DIMENSION_NO, TVector_, TVector_, TBox_, TRay_, TPlane_, TGeometry_, TAdapter_, TContainer_>;
    using EntityDistance = typename Base::EntityDistance;
    using BoxDistance = typename Base::BoxDistance;
    using IGM = typename Base::IGM;
    using IGM_Geometry = typename IGM::Geometry;

  public:
    using AD = typename Base::AD;
    using SI = typename Base::SI;
    using MortonLocationID = typename Base::MortonLocationID;
    using MortonLocationIDCR = typename Base::MortonLocationIDCR;
    using MortonNodeID = typename Base::MortonNodeID;
    using MortonNodeIDCR = typename Base::MortonNodeIDCR;
    using MortonChildID = typename Base::MortonChildID;

    using Node = typename Base::Node;

    using TGeometry = TGeometry_;
    using TVector = TVector_;
    using TBox = TBox_;
    using TRay = TRay_;
    using TPlane = TPlane_;
    using TEntity = typename Base::TEntity;
    using TEntityID = typename Base::TEntityID;
    using TContainer = typename Base::TContainer;

    static constexpr std::size_t DEFAULT_MAX_ELEMENT = DEFAULT_MAX_ELEMENT_IN_NODES;

  public: // Create
    // Ctors
    OrthoTreePoint() = default;
    inline explicit OrthoTreePoint(
      TContainer const& points,
      std::optional<depth_t> maxDepthIDIn = std::nullopt,
      std::optional<TBox> boxSpaceOptional = std::nullopt,
      std::size_t maxElementNoInNode = DEFAULT_MAX_ELEMENT,
      bool isParallelExec = false) noexcept
    {
      if (isParallelExec)
        this->template Create<true>(*this, points, maxDepthIDIn, std::move(boxSpaceOptional), maxElementNoInNode);
      else
        this->template Create<false>(*this, points, maxDepthIDIn, std::move(boxSpaceOptional), maxElementNoInNode);
    }

    template<typename EXEC_TAG>
    inline OrthoTreePoint(
      EXEC_TAG,
      TContainer const& points,
      std::optional<depth_t> maxDepthIDIn = std::nullopt,
      std::optional<TBox> boxSpaceOptional = std::nullopt,
      std::size_t maxElementNoInNode = DEFAULT_MAX_ELEMENT) noexcept
    {
      this->template Create<std::is_same_v<EXEC_TAG, ExecutionTags::Parallel>>(*this, points, maxDepthIDIn, std::move(boxSpaceOptional), maxElementNoInNode);
    }

  private:
    // Build the tree in depth-first order
    template<bool ARE_LOCATIONS_SORTED = false>
    inline constexpr void Build(detail::zip_view<std::vector<MortonLocationID>, std::span<TEntityID>>& locations) noexcept
    {
      struct NodeStackData
      {
        std::pair<MortonNodeID, Node> NodeInstance;
        typename detail::zip_view<std::vector<MortonLocationID>, std::span<TEntityID>>::iterator EndLocationIt;
      };
      auto nodeStack = std::vector<NodeStackData>(this->GetDepthNo());
      nodeStack[0] = NodeStackData{ *this->m_nodes.find(SI::GetRootKey()), locations.end() };
      this->m_nodes.clear();

      auto beginLocationIt = locations.begin();
      auto constexpr exitDepthID = depth_t(-1);
      for (depth_t depthID = 0; depthID != exitDepthID;)
      {
        auto& [node, endLocationIt] = nodeStack[depthID];
        std::size_t const elementNo = std::distance(beginLocationIt, endLocationIt);
        if ((0 < elementNo && elementNo <= this->m_maxElementNo && !node.second.IsAnyChildExist()) || depthID == this->m_maxDepthID)
        {
          node.second.ReplaceEntities(std::span(beginLocationIt.GetSecond(), endLocationIt.GetSecond()));
          beginLocationIt += elementNo;
        }

        if (beginLocationIt == endLocationIt)
        {
          this->m_nodes.emplace(std::move(node));
          --depthID;
          continue;
        }

        ++depthID;
        auto const examinedLevel = this->GetExaminationLevelID(depthID);
        auto const keyGenerator = typename SI::ChildKeyGenerator(node.first);
        auto const childChecker = typename SI::ChildCheckerFixedDepth(examinedLevel, (*beginLocationIt).GetFirst());
        auto const childID = childChecker.GetChildID(examinedLevel);
        auto childKey = keyGenerator.GetChildNodeKey(childID);
        node.second.AddChild(childID);
        if constexpr (ARE_LOCATIONS_SORTED)
        {
          nodeStack[depthID].EndLocationIt =
            std::partition_point(beginLocationIt, endLocationIt, [&](auto const& location) { return childChecker.Test(location.GetFirst()); });
        }
        else
        {
          nodeStack[depthID].EndLocationIt =
            std::partition(beginLocationIt, endLocationIt, [&](auto const& location) { return childChecker.Test(location.GetFirst()); });
        }

        nodeStack[depthID].NodeInstance.first = std::move(childKey);
        nodeStack[depthID].NodeInstance.second = this->CreateChild(node.second, childKey);
      }
    }

  public: // Create
    // Create
    template<bool IS_PARALLEL_EXEC = false>
    static void Create(
      OrthoTreePoint& tree,
      TContainer const& points,
      std::optional<depth_t> maxDepthIDIn = std::nullopt,
      std::optional<TBox> boxSpaceOptional = std::nullopt,
      std::size_t maxElementNoInNode = DEFAULT_MAX_ELEMENT) noexcept
    {
      auto const boxSpace = boxSpaceOptional.has_value() ? IGM::GetBoxAD(*boxSpaceOptional) : IGM::GetBoxOfPointsAD(points);
      auto const entityNo = points.size();

      auto const maxDepthID = (!maxDepthIDIn || maxDepthIDIn == depth_t{}) ? Base::EstimateMaxDepth(entityNo, maxElementNoInNode) : *maxDepthIDIn;
      tree.InitBase(boxSpace, maxDepthID, maxElementNoInNode, entityNo);
      if (points.empty())
        return;

      detail::reserve(tree.m_nodes, Base::EstimateNodeNumber(entityNo, maxDepthID, maxElementNoInNode));

      auto mortonIDs = std::vector<MortonLocationID>(entityNo);
      auto mainMemorySegment = tree.m_memoryResource.Allocate(entityNo);
      auto locationsZip = detail::zip_view(mortonIDs, mainMemorySegment.segment);

      using Location = decltype(locationsZip)::iterator::value_type;
      EXEC_POL_DEF(ept); // GCC 11.3
      std::transform(EXEC_POL_ADD(ept) points.begin(), points.end(), locationsZip.begin(), [&](auto const& point) -> Location {
        return { tree.GetLocationID(detail::getValuePart(point)), detail::getKeyPart(points, point) };
      });

      constexpr bool ARE_LOCATIONS_SORTED = IS_PARALLEL_EXEC;
      if constexpr (ARE_LOCATIONS_SORTED)
      {
        EXEC_POL_DEF(eps); // GCC 11.3
        std::sort(EXEC_POL_ADD(eps) locationsZip.begin(), locationsZip.end(), [&](Location const& l, Location const& r) { return l.first < r.first; });
      }

      tree.template Build<ARE_LOCATIONS_SORTED>(locationsZip);
    }

  public: // Edit functions
    // Insert entity into the tree with node rebalancing
    bool InsertWithRebalancing(TEntityID newEntityID, TVector const& newPoint, TContainer const& points) noexcept
    {
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), newPoint))
        return false;

      auto const entityLocation = this->GetRangeLocationMetaData(newPoint);
      auto const entityNodeKey = SI::GetHash(this->m_maxDepthID, entityLocation.LocID);
      auto const [parentNodeKey, parentDepthID] = this->FindSmallestNodeKeyWithDepth(entityNodeKey);
      if (!SI::IsValidKey(parentNodeKey))
        return false;

      return this->template InsertWithRebalancingBase<true>(parentNodeKey, parentDepthID, false, entityLocation, newEntityID, points);
    }


    // Insert entity into a node. If doInsertToLeaf is true: The smallest node will be chosen by the max depth. If doInsertToLeaf is false: The smallest existing level on the branch will be chosen.
    bool Insert(TEntityID newEntityID, TVector const& newPoint, bool doInsertToLeaf = false) noexcept
    {
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), newPoint))
        return false;

      auto const entityNodeKey = this->GetNodeID(newPoint);
      auto const smallestNodeKey = this->FindSmallestNodeKey(entityNodeKey);
      if (!SI::IsValidKey(smallestNodeKey))
        return false;

      return this->template InsertWithoutRebalancingBase<true>(smallestNodeKey, entityNodeKey, newEntityID, doInsertToLeaf);
    }


    // Insert entity into a node, if there is no entity within the same location by tolerance.
    bool InsertUnique(TEntityID newEntityID, TVector const& newPoint, TGeometry tolerance, TContainer const& points, bool doInsertToLeaf = false)
    {
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), newPoint))
        return false;

      auto const entityLocation = this->GetRangeLocationMetaData(newPoint);
      auto const entityNodeKey = SI::GetHash(this->m_maxDepthID, entityLocation.LocID);
      auto const [parentNodeKey, parentDepthID] = this->FindSmallestNodeKeyWithDepth(entityNodeKey);
      if (!SI::IsValidKey(parentNodeKey))
        return false;

      auto const nearestEntityList = this->GetNearestNeighbors(newPoint, 1, tolerance, points);
      if (!nearestEntityList.empty())
        return false;

      if (doInsertToLeaf)
        return this->template InsertWithoutRebalancingBase<true>(parentNodeKey, entityNodeKey, newEntityID, true);
      else
        return this->template InsertWithRebalancingBase<true>(parentNodeKey, parentDepthID, false, entityLocation, newEntityID, points);
    }


    // Erase an id. Traverse all node if it is needed, which has major performance penalty.
    template<bool DO_UPDATE_ENTITY_IDS = Base::IS_CONTIGOUS_CONTAINER>
    constexpr bool EraseEntity(TEntityID entityID) noexcept
    {
      return this->template EraseEntityBase<false, DO_UPDATE_ENTITY_IDS>(entityID);
    }


    // Erase id, aided with the original point
    template<bool DO_UPDATE_ENTITY_IDS = Base::IS_CONTIGOUS_CONTAINER>
    bool Erase(TEntityID entitiyID, TVector const& entityOriginalPoint) noexcept
    {
      auto const nodeKey = this->FindSmallestNode(entityOriginalPoint);
      if (!SI::IsValidKey(nodeKey))
        return false; // old box is not in the handled space domain

      auto& entityNode = detail::at(this->m_nodes, nodeKey);
      bool const isEntityRemoved = this->RemoveNodeEntity(entityNode, entitiyID);
      if (!isEntityRemoved)
        return false; // id was not registered previously.

      if constexpr (DO_UPDATE_ENTITY_IDS)
      {
        for (auto& [key, node] : this->m_nodes)
          node.DecreaseEntityIDs(entitiyID);
      }

      this->RemoveNodeIfPossible(entityNode);

      return true;
    }


    // Update id by the new point information
    bool Update(TEntityID entityID, TVector const& newPoint, bool doesInsertToLeaf = false) noexcept
    {
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), newPoint))
        return false;

      if (!this->template EraseEntity<false>(entityID))
        return false;

      return this->Insert(entityID, newPoint, doesInsertToLeaf);
    }


    // Update id by the new point information and the erase part is aided by the old point geometry data
    bool Update(TEntityID entityID, TVector const& oldPoint, TVector const& newPoint, bool doesInsertToLeaf = false) noexcept
    {
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), newPoint))
        return false;

      if (!this->Erase<false>(entityID, oldPoint))
        return false;

      return this->Insert(entityID, newPoint, doesInsertToLeaf);
    }


    // Update id with rebalancing by the new point information
    bool Update(TEntityID entityID, TVector const& newPoint, TContainer const& points) noexcept
    {
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), newPoint))
        return false;

      if (!this->EraseEntity<false>(entityID))
        return false;

      return this->InsertWithRebalancing(entityID, newPoint, points);
    }


    // Update id with rebalancing by the new point information and the erase part is aided by the old point geometry data
    bool Update(TEntityID entityID, TVector const& oldPoint, TVector const& newPoint, TContainer const& points) noexcept
    {
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), newPoint))
        return false;

      if (!this->Erase<false>(entityID, oldPoint))
        return false;

      return this->InsertWithRebalancing(entityID, newPoint, points);
    }

  public: // Search functions
    bool Contains(TVector const& searchPoint, TContainer const& points, TGeometry tolerance) const noexcept
    {
      auto const smallestNodeKey = this->FindSmallestNode(searchPoint);
      if (!SI::IsValidKey(smallestNodeKey))
        return false;

      auto const& entities = this->GetNodeEntities(smallestNodeKey);
      return std::any_of(entities.begin(), entities.end(), [&](auto const& entityID) {
        return AD::ArePointsEqual(searchPoint, detail::at(points, entityID), tolerance);
      });
    }

    // Range search
    template<bool DOES_LEAF_NODE_CONTAIN_ELEMENT_ONLY = false>
    std::vector<TEntityID> RangeSearch(TBox const& range, TContainer const& points) const noexcept
    {
      auto foundEntityIDs = std::vector<TEntityID>();
      this->template RangeSearchBaseRoot<false, DOES_LEAF_NODE_CONTAIN_ELEMENT_ONLY>(range, points, foundEntityIDs);
      return foundEntityIDs;
    }

    // Hyperplane intersection (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> PlaneSearch(
      TGeometry const& distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance, TContainer const& points) const noexcept
    {
      return this->PlaneIntersectionBase(distanceOfOrigo, planeNormal, tolerance, points);
    }

    // Hyperplane intersection using built-in plane
    inline std::vector<TEntityID> PlaneSearch(TPlane const& plane, TGeometry tolerance, TContainer const& points) const noexcept
    {
      return this->PlaneIntersectionBase(AD::GetPlaneOrigoDistance(plane), AD::GetPlaneNormal(plane), tolerance, points);
    }

    // Hyperplane segmentation, get all elements in positive side (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> PlanePositiveSegmentation(
      TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance, TContainer const& points) const noexcept
    {
      return this->PlanePositiveSegmentationBase(distanceOfOrigo, planeNormal, tolerance, points);
    }

    // Hyperplane segmentation, get all elements in positive side (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> PlanePositiveSegmentation(TPlane const& plane, TGeometry tolerance, TContainer const& points) const noexcept
    {
      return this->PlanePositiveSegmentationBase(AD::GetPlaneOrigoDistance(plane), AD::GetPlaneNormal(plane), tolerance, points);
    }

    // Hyperplane segmentation, get all elements in positive side (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> FrustumCulling(std::span<TPlane const> const& boundaryPlanes, TGeometry tolerance, TContainer const& points) const noexcept
    {
      return this->FrustumCullingBase(boundaryPlanes, tolerance, points);
    }


  private: // K Nearest Neighbor helpers
    static inline void AddEntityDistance(
      auto const& entities,
      TVector const& searchPoint,
      TContainer const& points,
      std::vector<EntityDistance>& neighborEntities,
      std::size_t neighborNo,
      TGeometry& maxDistanceWithin) noexcept
    {
      for (auto const entityID : entities)
      {
        const auto distance = AD::Distance(searchPoint, detail::at(points, entityID));

        // maxDistanceWithin is implemented for tolerance handling: distance should be less than maxDistanceWithin
        if (distance >= maxDistanceWithin)
          continue;

        if (neighborEntities.size() < neighborNo - 1)
          neighborEntities.push_back({ { distance }, entityID });
        else
        {
          if (neighborEntities.size() < neighborNo)
          {
            std::make_heap(neighborEntities.begin(), neighborEntities.end());
            neighborEntities.push_back({ { distance }, entityID });
          }
          else
          {
            std::pop_heap(neighborEntities.begin(), neighborEntities.end());
            neighborEntities.back() = { { distance }, entityID };
          }
          std::push_heap(neighborEntities.begin(), neighborEntities.end());
          maxDistanceWithin = neighborEntities.front().Distance;
        }
      }
    }

    static inline constexpr std::vector<TEntityID> ConvertEntityDistanceToList(std::vector<EntityDistance>& neighborEntities, std::size_t neighborNo) noexcept
    {
      auto entityIDs = std::vector<TEntityID>();
      if (neighborEntities.empty())
        return entityIDs;

      if (neighborEntities.size() < neighborNo)
        std::sort(neighborEntities.begin(), neighborEntities.end());
      else
        std::sort_heap(neighborEntities.begin(), neighborEntities.end());

      auto const entityNo = neighborEntities.size();
      entityIDs.resize(entityNo);
      for (std::size_t i = 0; i < entityNo; ++i)
        entityIDs[i] = neighborEntities[i].EntityID;

      return entityIDs;
    }

    inline constexpr IGM::Geometry GetNodeWallDistance(
      TVector const& searchPoint, MortonNodeIDCR key, [[maybe_unused]] Node const& node, bool isInsideConsideredAsZero) const noexcept
    {
      auto const depthID = SI::GetDepthID(key);
      auto const& halfSize = this->GetNodeSize(depthID + 1);
      auto const& centerPoint = GetNodeCenterMacro(this, key, node);
      return IGM::GetBoxWallDistanceAD(searchPoint, centerPoint, halfSize, isInsideConsideredAsZero);
    }

    void VisitNodesInDFSWithChildrenEdit(
      depth_t stackID, std::pair<Node const*, TGeometry> const& nodeWithDistance, auto const& procedure, auto const& childNodeKeyEditor) const noexcept
    {
      if (!procedure(nodeWithDistance))
        return;

      auto const childStackID = stackID + 1;
      for (auto const& childNodeWithDistance : childNodeKeyEditor(nodeWithDistance.first->GetChildren(), stackID))
        this->VisitNodesInDFSWithChildrenEdit(childStackID, childNodeWithDistance, procedure, childNodeKeyEditor);
    }

  public:
    // Get K Nearest Neighbor sorted by distance (point distance should be less than maxDistanceWithin, it is used as a Tolerance check)
    std::vector<TEntityID> GetNearestNeighbors(
      TVector const& searchPoint, std::size_t neighborNo, TGeometry maxDistanceWithin, TContainer const& points) const noexcept
    {
      auto neighborEntities = std::vector<EntityDistance>();
      neighborEntities.reserve(neighborNo);

      auto smallestNodeKey = this->FindSmallestNodeKey(this->template GetNodeID<true>(searchPoint));
      if (!SI::IsValidKey(smallestNodeKey))
        smallestNodeKey = SI::GetRootKey();

      auto farestEntityDistance = maxDistanceWithin;
      // Parent checks (in a usual case parents do not have entities)
      for (auto nodeKey = smallestNodeKey; SI::IsValidKey(nodeKey); nodeKey = SI::GetParentKey(nodeKey))
        AddEntityDistance(this->GetNodeEntities(nodeKey), searchPoint, points, neighborEntities, neighborNo, farestEntityDistance);

      // Search in itself and the children
      auto childrenDistanceStack = std::vector<std::vector<std::pair<Node const*, TGeometry>>>(this->GetMaxDepthID());
      for (auto nodeKey = smallestNodeKey, prevNodeKey = MortonNodeID{}; SI::IsValidKey(nodeKey);
           prevNodeKey = nodeKey, nodeKey = SI::GetParentKey(nodeKey))
      {
        auto const node = this->GetNode(nodeKey);
        auto const wallDistance = this->GetNodeWallDistance(searchPoint, nodeKey, node, false);
        this->VisitNodesInDFSWithChildrenEdit(
          0,
          { &node, wallDistance },
          [&](std::pair<Node const*, TGeometry> const& nodeDistance) {
            auto const& [childNode, childNodeDistance] = nodeDistance;
            auto const childNodeKey = childNode->GetKey();
            if (childNodeKey == nodeKey)
              return true; // Self check was already done.

            if (childNodeKey == prevNodeKey)
              return false; // Previous subtree was already checked.

            if (childNodeDistance > farestEntityDistance)
              return false;

            AddEntityDistance(this->GetNodeEntities(*childNode), searchPoint, points, neighborEntities, neighborNo, farestEntityDistance);
            return true;
          },
          [&](auto const& children, depth_t stackID) -> std::span<std::pair<Node const*, TGeometry>> {
            if (children.empty())
              return {};

            auto& childrenDistance = childrenDistanceStack[stackID];
            childrenDistance.clear();
            for (MortonNodeIDCR childNodeKey : children)
            {
              auto const& childNode = this->m_nodes.at(childNodeKey);
              auto const wallDistance = this->GetNodeWallDistance(searchPoint, childNodeKey, childNode, true);
              if (wallDistance > farestEntityDistance)
                continue;

              childrenDistance.emplace_back(&childNode, wallDistance);
            }

            std::sort(childrenDistance.begin(), childrenDistance.end(), [&](auto const& leftDistance, auto const& rightDistance) {
              return leftDistance.second < rightDistance.second;
            });

            return std::span(childrenDistance);
          });

        if (farestEntityDistance < wallDistance)
          break;
      }

      return ConvertEntityDistanceToList(neighborEntities, neighborNo);
    }

    // Get K Nearest Neighbor sorted by distance
    inline std::vector<TEntityID> GetNearestNeighbors(TVector const& searchPoint, std::size_t neighborNo, TContainer const& points) const noexcept
    {
      return this->GetNearestNeighbors(searchPoint, neighborNo, std::numeric_limits<TGeometry>::max(), points);
    }
  };


  // OrthoTreeBoundingBox: Non-owning container which spatially organize axis aligned bounding box (AABB) ids in N dimension space into a hash-table by Morton Z order.
  // DO_SPLIT_PARENT_ENTITIES: Those items which are not fit in the child nodes may be stored in the children/grand-children instead of the parent.
  template<
    dim_t DIMENSION_NO,
    typename TVector_,
    typename TBox_,
    typename TRay_,
    typename TPlane_,
    typename TGeometry_ = double,
    bool DO_SPLIT_PARENT_ENTITIES = true,
    typename TAdapter_ = AdaptorGeneral<DIMENSION_NO, TVector_, TBox_, TRay_, TPlane_, TGeometry_>,
    typename TContainer_ = std::span<TBox_ const>>
  class OrthoTreeBoundingBox final : public OrthoTreeBase<DIMENSION_NO, TBox_, TVector_, TBox_, TRay_, TPlane_, TGeometry_, TAdapter_, TContainer_>
  {
  protected:
    using Base = OrthoTreeBase<DIMENSION_NO, TBox_, TVector_, TBox_, TRay_, TPlane_, TGeometry_, TAdapter_, TContainer_>;
    using EntityDistance = typename Base::EntityDistance;
    using BoxDistance = typename Base::BoxDistance;
    template<typename T>
    using DimArray = std::array<T, DIMENSION_NO>;
    using IGM = typename Base::IGM;
    using IGM_Geometry = typename IGM::Geometry;

  public:
    using AD = typename Base::AD;
    using SI = typename Base::SI;
    using MortonLocationID = typename Base::MortonLocationID;
    using MortonLocationIDCR = typename Base::MortonLocationIDCR;
    using MortonNodeID = typename Base::MortonNodeID;
    using MortonNodeIDCR = typename Base::MortonNodeIDCR;
    using MortonChildID = typename Base::MortonChildID;

    using Node = typename Base::Node;

    using TGeometry = TGeometry_;
    using TVector = TVector_;
    using TBox = TBox_;
    using TRay = TRay_;
    using TPlane = TPlane_;
    using TEntity = typename Base::TEntity;
    using TEntityID = typename Base::TEntityID;
    using TContainer = typename Base::TContainer;

    static constexpr std::size_t DEFAULT_MAX_ELEMENT = DEFAULT_MAX_ELEMENT_IN_NODES;

  public: // Ctors
    OrthoTreeBoundingBox() = default;
    inline explicit OrthoTreeBoundingBox(
      TContainer const& boxes,
      std::optional<depth_t> maxDepthID = std::nullopt,
      std::optional<TBox> boxSpaceOptional = std::nullopt,
      std::size_t nElementMaxInNode = DEFAULT_MAX_ELEMENT,
      bool isParallelExec = false) noexcept
    {
      if (isParallelExec)
        this->template Create<true>(*this, boxes, maxDepthID, std::move(boxSpaceOptional), nElementMaxInNode);
      else
        this->template Create<false>(*this, boxes, maxDepthID, std::move(boxSpaceOptional), nElementMaxInNode);
    }

    template<typename EXEC_TAG>
    inline OrthoTreeBoundingBox(
      EXEC_TAG,
      TContainer const& boxes,
      std::optional<depth_t> maxDepthID = std::nullopt,
      std::optional<TBox> boxSpaceOptional = std::nullopt,
      std::size_t nElementMaxInNode = DEFAULT_MAX_ELEMENT) noexcept
    {
      this->template Create<std::is_same_v<EXEC_TAG, ExecutionTags::Parallel>>(*this, boxes, maxDepthID, std::move(boxSpaceOptional), nElementMaxInNode);
    }

  private: // Aid functions
    using LocationIterator = typename detail::zip_view<std::vector<typename SI::RangeLocationMetaData>, std::span<TEntityID>>::iterator;

    struct SplitItem
    {
      MortonChildID SegmentID;
      LocationIterator LocationIt;
    };
    using SplitEntityContianer = std::vector<SplitItem>;
    using SplitEntityIterator = std::vector<SplitItem>::iterator;

    struct NodeProcessingData
    {
      std::pair<MortonNodeID, Node> NodeInstance;
      LocationIterator EndLocationIt;
    };
    struct SplitEntityProcessingData
    {
      SplitEntityContianer Entities;
      SplitEntityIterator BeginIt;
    };

    void SplitEntityLocation(std::vector<SplitItem>& splitEntities, LocationIterator const& locationIt) const noexcept
    {
      auto const originalSize = splitEntities.size();
      this->TraverseSplitChildren(
        (*locationIt).GetFirst(),
        [originalSize, &splitEntities](std::size_t permutationNo) { splitEntities.resize(originalSize + permutationNo); },
        [&](std::size_t permutationID, MortonChildID segmentID) { splitEntities[originalSize + permutationID] = { segmentID, locationIt }; });
    }

    template<bool ARE_LOCATIONS_SORTED>
    inline constexpr void ProcessNodeWithoutSplitEntities(depth_t depthID, LocationIterator& locationIt, NodeProcessingData& nodeProcessingData)
    {
      auto& [node, endLocationIt] = nodeProcessingData;

      auto const subtreeEntityNo = std::size_t(std::distance(locationIt, endLocationIt));
      if (subtreeEntityNo == 0)
        return;

      auto nodeEntityNo = subtreeEntityNo;
      auto stuckedEndLocationIt = endLocationIt;
      if (subtreeEntityNo > this->m_maxElementNo && depthID < this->m_maxDepthID)
      {
        if constexpr (ARE_LOCATIONS_SORTED)
        {
          stuckedEndLocationIt =
            std::partition_point(locationIt, endLocationIt, [depthID](auto const& location) { return location.GetFirst().DepthID == depthID; });
        }
        else
        {
          stuckedEndLocationIt =
            std::partition(locationIt, endLocationIt, [depthID](auto const& location) { return location.GetFirst().DepthID == depthID; });
        }

        nodeEntityNo = std::size_t(std::distance(locationIt, stuckedEndLocationIt));
      }

      if (nodeEntityNo == 0)
        return;

      node.second.ReplaceEntities(std::span(locationIt.GetSecond(), stuckedEndLocationIt.GetSecond()));
      locationIt += nodeEntityNo;
    }


    template<bool ARE_LOCATIONS_SORTED>
    inline constexpr void ProcessNodeWithSplitEntities(
      depth_t depthID,
      LocationIterator& locationIt,
      NodeProcessingData& nodeProcessingData,
      SplitEntityProcessingData& splitEntityProcessingData,
      SplitEntityProcessingData* parentSplitEntityProcessingData) noexcept
    {
      auto& [node, endLocationIt] = nodeProcessingData;
      auto& [splitEntities, splitEntityBeginIt] = splitEntityProcessingData;

      auto const subtreeEntityNo = std::size_t(std::distance(locationIt, endLocationIt));
      auto nodeEntityNo = subtreeEntityNo;

      auto splitEntityNoFromParent = std::size_t{};

      bool isLeafNode = depthID == this->m_maxDepthID;
      if (parentSplitEntityProcessingData && !parentSplitEntityProcessingData->Entities.empty())
      {
        auto const segmentID = SI::GetChildID(node.first);
        auto const splitEntitiesEndIt =
          std::partition(parentSplitEntityProcessingData->BeginIt, parentSplitEntityProcessingData->Entities.end(), [segmentID](auto const& childEntities) {
            return childEntities.SegmentID == segmentID;
          });

        splitEntityNoFromParent = size_t(std::distance(parentSplitEntityProcessingData->BeginIt, splitEntitiesEndIt));
        nodeEntityNo += splitEntityNoFromParent;
      }
      isLeafNode |= nodeEntityNo <= this->m_maxElementNo;

      auto stuckedEndLocationIt = endLocationIt;
      auto stuckedAndNonSplittableEndLocationIt = endLocationIt;
      if (!isLeafNode)
      {
        auto const partitioner = [depthID](auto const& location) {
          return location.GetFirst().DepthID == depthID;
        };
        if constexpr (ARE_LOCATIONS_SORTED)
        {
          stuckedEndLocationIt = std::partition_point(locationIt, endLocationIt, partitioner);
        }
        else
        {
          stuckedEndLocationIt = std::partition(locationIt, endLocationIt, partitioner);
        }

        stuckedAndNonSplittableEndLocationIt = std::partition(locationIt, stuckedEndLocationIt, [](auto const& location) {
          return SI::IsAllChildTouched(location.GetFirst().TouchedDimensionsFlag);
        });
      }

      std::size_t const stuckedAndNonSplittableEndLocationNo = std::distance(locationIt, stuckedAndNonSplittableEndLocationIt);
      node.second.GetEntitySegment() = this->m_memoryResource.Allocate(splitEntityNoFromParent + stuckedAndNonSplittableEndLocationNo);
      auto& entityIDs = node.second.GetEntities();
      if (splitEntityNoFromParent > 0)
      {
        LOOPIVDEP
        for (std::size_t i = 0; i < splitEntityNoFromParent; ++i)
        {
          entityIDs[i] = (*parentSplitEntityProcessingData->BeginIt->LocationIt).GetSecond();
          ++parentSplitEntityProcessingData->BeginIt;
        }
      }

      if (stuckedAndNonSplittableEndLocationNo){
        if constexpr (std::is_trivially_copyable_v<TEntityID>)
          std::memcpy(entityIDs.data() + splitEntityNoFromParent, &(*locationIt.GetSecond()), stuckedAndNonSplittableEndLocationNo * sizeof(TEntityID));
        else
          std::copy(locationIt.GetSecond(), stuckedAndNonSplittableEndLocationIt.GetSecond(), entityIDs.begin() + splitEntityNoFromParent);

        locationIt += stuckedAndNonSplittableEndLocationNo;
      }

      std::size_t const splitEntitiesNo = std::distance(stuckedAndNonSplittableEndLocationIt, stuckedEndLocationIt);
      for (std::size_t i = 0; i < splitEntitiesNo; ++i)
      {
        this->SplitEntityLocation(splitEntities, locationIt);
        ++locationIt;
      }
      splitEntityBeginIt = splitEntities.begin();
    }

    template<bool ARE_LOCATIONS_SORTED>
    inline constexpr void CreateProcessingData(
      depth_t examinedLevelID,
      SI::ChildKeyGenerator const& keyGenerator,
      LocationIterator const& locationIt,
      NodeProcessingData& parentNodeProcessingData,
      NodeProcessingData& nodeProcessingData) const noexcept
    {
      auto const childChecker = typename SI::ChildCheckerFixedDepth(examinedLevelID, (*locationIt).GetFirst().LocID);
      auto const childID = childChecker.GetChildID(examinedLevelID);
      auto childKey = keyGenerator.GetChildNodeKey(childID);

      parentNodeProcessingData.NodeInstance.second.AddChild(childID);
      if constexpr (ARE_LOCATIONS_SORTED)
      {
        nodeProcessingData.EndLocationIt = std::partition_point(locationIt, parentNodeProcessingData.EndLocationIt, [&](auto const& location) {
          return childChecker.Test(location.GetFirst().LocID);
        });
      }
      else
      {
        nodeProcessingData.EndLocationIt = std::partition(locationIt, parentNodeProcessingData.EndLocationIt, [&](auto const& location) {
          return childChecker.Test(location.GetFirst().LocID);
        });
      }

      nodeProcessingData.NodeInstance.second = this->CreateChild(parentNodeProcessingData.NodeInstance.second, childKey);
      nodeProcessingData.NodeInstance.first = std::move(childKey);
    }

    template<bool ARE_LOCATIONS_SORTED>
    inline constexpr void CreateProcessingDataWithSplitEntities(
      depth_t examinedLevelID,
      SI::ChildKeyGenerator const& keyGenerator,
      LocationIterator const& locationIt,
      SplitEntityProcessingData const& parentSplitEntityProcessingData,
      NodeProcessingData& parentNodeProcessingData,
      NodeProcessingData& nodeProcessingData) const noexcept
    {
      if (locationIt == parentNodeProcessingData.EndLocationIt)
      {
        parentNodeProcessingData.NodeInstance.second.AddChild(parentSplitEntityProcessingData.BeginIt->SegmentID);

        auto childKey = keyGenerator.GetChildNodeKey(parentSplitEntityProcessingData.BeginIt->SegmentID);
        nodeProcessingData.EndLocationIt = parentNodeProcessingData.EndLocationIt;
        nodeProcessingData.NodeInstance.second = this->CreateChild(parentNodeProcessingData.NodeInstance.second, childKey);
        nodeProcessingData.NodeInstance.first = std::move(childKey);
      }
      else
      {
        this->template CreateProcessingData<ARE_LOCATIONS_SORTED>(examinedLevelID, keyGenerator, locationIt, parentNodeProcessingData, nodeProcessingData);
      }
    }


    // Build the tree in depth-first order
    template<bool ARE_LOCATIONS_SORTED, typename TResultContainer>
    inline constexpr void BuildSubtree(
      LocationIterator const& rootBeginLocationIt,
      LocationIterator const& rootEndLocationIt,
      std::pair<MortonNodeID, Node> const& rootNode,
      TResultContainer& nodes) noexcept
    {
      auto nodeStack = std::vector<NodeProcessingData>(this->GetDepthNo());
      nodeStack[0] = NodeProcessingData{ rootNode, rootEndLocationIt };

      auto splitEntityStack = std::vector<SplitEntityProcessingData>(this->GetDepthNo());
      splitEntityStack[0].BeginIt = splitEntityStack[0].Entities.begin();

      auto locationIt = rootBeginLocationIt;
      auto constexpr exitDepthID = depth_t(-1);
      for (depth_t depthID = 0; depthID != exitDepthID;)
      {
        if (!nodeStack[depthID].NodeInstance.second.IsAnyChildExist())
        {
          if constexpr (DO_SPLIT_PARENT_ENTITIES)
          {
            this->template ProcessNodeWithSplitEntities<ARE_LOCATIONS_SORTED>(
              depthID, locationIt, nodeStack[depthID], splitEntityStack[depthID], depthID > 0 ? &splitEntityStack[depthID - 1] : nullptr);
          }
          else
          {
            this->template ProcessNodeWithoutSplitEntities<ARE_LOCATIONS_SORTED>(depthID, locationIt, nodeStack[depthID]);
          }
        }

        auto const isNonSplitEntitesProcessed = locationIt == nodeStack[depthID].EndLocationIt;
        bool canNodeBeCommited = isNonSplitEntitesProcessed;
        if constexpr (DO_SPLIT_PARENT_ENTITIES)
        {
          auto const isSplitEntitiesProcessed =
            splitEntityStack[depthID].Entities.empty() || splitEntityStack[depthID].BeginIt == splitEntityStack[depthID].Entities.end();

          canNodeBeCommited &= isSplitEntitiesProcessed;
        }

        if (canNodeBeCommited || depthID == this->m_maxDepthID)
        {
          assert(canNodeBeCommited);
          detail::emplace(nodes, std::move(nodeStack[depthID].NodeInstance));
          splitEntityStack[depthID].Entities.clear();
          --depthID;
          continue;
        }

        ++depthID;
        auto const examinedLevelID = this->GetExaminationLevelID(depthID);
        auto const keyGenerator = typename SI::ChildKeyGenerator(nodeStack[depthID - 1].NodeInstance.first);
        if constexpr (DO_SPLIT_PARENT_ENTITIES)
        {
          this->template CreateProcessingDataWithSplitEntities<ARE_LOCATIONS_SORTED>(
            examinedLevelID, keyGenerator, locationIt, splitEntityStack[depthID - 1], nodeStack[depthID - 1], nodeStack[depthID]);
        }
        else
        {
          this->template CreateProcessingData<ARE_LOCATIONS_SORTED>(examinedLevelID, keyGenerator, locationIt, nodeStack[depthID - 1], nodeStack[depthID]);
        }
      }
    }

  public: // Create
    // Create
    template<bool IS_PARALLEL_EXEC = false>
    static void Create(
      OrthoTreeBoundingBox& tree,
      TContainer const& boxes,
      std::optional<depth_t> maxDepthIn = std::nullopt,
      std::optional<TBox> boxSpaceOptional = std::nullopt,
      std::size_t maxElementNoInNode = DEFAULT_MAX_ELEMENT) noexcept
    {
      auto const boxSpace = boxSpaceOptional.has_value() ? IGM::GetBoxAD(*boxSpaceOptional) : IGM::GetBoxOfBoxesAD(boxes);
      auto const entityNo = boxes.size();
      auto const maxDepthID = (!maxDepthIn || maxDepthIn == depth_t{}) ? Base::EstimateMaxDepth(entityNo, maxElementNoInNode) : *maxDepthIn;
      tree.InitBase(boxSpace, maxDepthID, maxElementNoInNode, DO_SPLIT_PARENT_ENTITIES ? std::size_t(1.3 * entityNo) : entityNo);

      if (entityNo == 0)
        return;

      auto mortonIDs = std::vector<typename SI::RangeLocationMetaData>(entityNo);
      auto entityIDs = std::vector<TEntityID>();
      auto entityIDsView = std::span<TEntityID>();
      if constexpr (DO_SPLIT_PARENT_ENTITIES)
      {
        entityIDs.resize(entityNo);
        entityIDsView = std::span(entityIDs);
      }
      else
      {
        entityIDsView = tree.m_memoryResource.Allocate(entityNo).segment;
      }

      auto locationsZip = detail::zip_view(mortonIDs, entityIDsView);
      using Location = decltype(locationsZip)::iterator::value_type;

      EXEC_POL_DEF(epf); // GCC 11.3
      std::transform(EXEC_POL_ADD(epf) boxes.begin(), boxes.end(), locationsZip.begin(), [&tree, &boxes](auto const& box) -> Location {
        return { tree.GetRangeLocationMetaData(detail::getValuePart(box)), detail::getKeyPart(boxes, box) };
      });

      constexpr bool ARE_LOCATIONS_SORTED = IS_PARALLEL_EXEC;
      if constexpr (ARE_LOCATIONS_SORTED)
      {
        EXEC_POL_DEF(eps); // GCC 11.3
        std::sort(EXEC_POL_ADD(eps) locationsZip.begin(), locationsZip.end(), [](Location const& l, Location const& r) {
          auto const& lm = l.first;
          auto const& rm = r.first;

          if (lm.LocID == rm.LocID)
            return lm.DepthID < rm.DepthID;

          return lm.LocID < rm.LocID;
        });
      }

      auto const rootNode = *tree.m_nodes.begin();
      tree.m_nodes.clear();
      detail::reserve(tree.m_nodes, Base::EstimateNodeNumber(entityNo, maxDepthID, maxElementNoInNode));
      tree.template BuildSubtree<ARE_LOCATIONS_SORTED>(locationsZip.begin(), locationsZip.end(), rootNode, tree.m_nodes);
    }

  public: // Edit functions
    bool InsertWithRebalancing(TEntityID newEntityID, TBox const& newBox, TContainer const& boxes) noexcept
    {
      if (!IGM::DoesRangeContainBoxAD(this->m_grid.GetBoxSpace(), newBox))
        return false;

      auto const entityLocation = this->GetRangeLocationMetaData(newBox);
      auto const entityNodeKey = SI::GetHashAtDepth(entityLocation, this->GetMaxDepthID());

      auto const parentNodeKey = this->FindSmallestNodeKey(entityNodeKey);

      return this->template InsertWithRebalancingBase<!DO_SPLIT_PARENT_ENTITIES>(
        parentNodeKey, SI::GetDepthID(parentNodeKey), DO_SPLIT_PARENT_ENTITIES, entityLocation, newEntityID, boxes);
    }


    // Insert item into a node. If doInsertToLeaf is true: The smallest node will be chosen by the max depth. If doInsertToLeaf is false: The smallest existing level on the branch will be chosen.
    bool Insert(TEntityID newEntityID, TBox const& newBox, bool doInsertToLeaf = false) noexcept
    {
      if (!IGM::DoesRangeContainBoxAD(this->m_grid.GetBoxSpace(), newBox))
        return false;

      auto const location = this->GetRangeLocationMetaData(newBox);
      auto const entityNodeKey = SI::GetHashAtDepth(location, this->GetMaxDepthID());
      auto const smallestNodeKey = this->FindSmallestNodeKey(entityNodeKey);
      if (!SI::IsValidKey(smallestNodeKey))
        return false; // new box is not in the handled space domain

      auto const shoudlCreateChildrenOnly = location.DepthID != this->GetMaxDepthID() && doInsertToLeaf && DO_SPLIT_PARENT_ENTITIES;
      if (shoudlCreateChildrenOnly)
      {
        auto const childKeyGenerator = typename SI::ChildKeyGenerator(SI::GetHashAtDepth(location, this->GetMaxDepthID()));
        for (auto const childID : this->GetSplitChildSegments(location))
        {
          if (!this->template InsertWithoutRebalancingBase<!DO_SPLIT_PARENT_ENTITIES>(
                smallestNodeKey, childKeyGenerator.GetChildNodeKey(childID), newEntityID, doInsertToLeaf))
            return false;
        }
      }
      else
      {
        if (!this->template InsertWithoutRebalancingBase<!DO_SPLIT_PARENT_ENTITIES>(smallestNodeKey, entityNodeKey, newEntityID, doInsertToLeaf))
          return false;
      }

      return true;
    }


  private:
    template<depth_t REMAINING_DEPTH>
    bool DoEraseRec(MortonNodeIDCR nodeKey, TEntityID entityID) noexcept
    {
      auto& node = detail::at(this->m_nodes, nodeKey);
      auto isThereAnyErased = this->RemoveNodeEntity(node, entityID);
      if constexpr (REMAINING_DEPTH > 0)
      {
        auto const& childKeys = node.GetChildren();
        auto const childKeysCopy = std::vector(childKeys.begin(), childKeys.end()); // Copy required because of RemoveNodeIfPossible()
        for (MortonNodeID childKey : childKeysCopy)
          isThereAnyErased |= DoEraseRec<REMAINING_DEPTH - 1>(childKey, entityID);
      }
      this->RemoveNodeIfPossible(node);

      return isThereAnyErased;
    }


  public:
    // Erase id, aided with the original bounding box
    template<bool DO_UPDATE_ENTITY_IDS = Base::IS_CONTIGOUS_CONTAINER>
    bool Erase(TEntityID entityIDToErase, TBox const& box) noexcept
    {
      auto const smallestNodeKey = this->FindSmallestNode(box);
      if (!SI::IsValidKey(smallestNodeKey))
        return false; // old box is not in the handled space domain

      if (DoEraseRec<DO_SPLIT_PARENT_ENTITIES>(smallestNodeKey, entityIDToErase))
      {
        if constexpr (DO_UPDATE_ENTITY_IDS)
        {
          for (auto& [key, node] : this->m_nodes)
            node.DecreaseEntityIDs(entityIDToErase);
        }
        return true;
      }
      else
        return false;
    }

    // Erase an id. Traverse all node if it is needed, which has major performance penalty.
    template<bool DO_UPDATE_ENTITY_IDS = Base::IS_CONTIGOUS_CONTAINER>
    constexpr bool EraseEntity(TEntityID entityID) noexcept
    {
      return this->template EraseEntityBase<DO_SPLIT_PARENT_ENTITIES, false>(entityID);
    }

    // Update id by the new bounding box information
    bool Update(TEntityID entityID, TBox const& boxNew, bool doInsertToLeaf = false) noexcept
    {
      if (!IGM::DoesRangeContainBoxAD(this->m_grid.GetBoxSpace(), boxNew))
        return false;

      if (!this->template EraseEntity<false>(entityID))
        return false;

      return this->Insert(entityID, boxNew, doInsertToLeaf);
    }

    // Update id by the new bounding box information and the erase part is aided by the old bounding box geometry data
    bool Update(TEntityID entityID, TBox const& oldBox, TBox const& newBox, bool doInsertToLeaf = false) noexcept
    {
      if (!IGM::DoesRangeContainBoxAD(this->m_grid.GetBoxSpace(), newBox))
        return false;

      if constexpr (!DO_SPLIT_PARENT_ENTITIES)
        if (this->FindSmallestNode(oldBox) == this->FindSmallestNode(newBox))
          return true;

      if (!this->Erase<false>(entityID, oldBox))
        return false; // entityID was not registered previously.

      return this->Insert(entityID, newBox, doInsertToLeaf);
    }

    // Update id with rebalancing by the new bounding box information
    bool Update(TEntityID entityID, TBox const& boxNew, TContainer const& boxes) noexcept
    {
      if (!IGM::DoesRangeContainBoxAD(this->m_grid.GetBoxSpace(), boxNew))
        return false;

      if (!this->EraseEntity<false>(entityID))
        return false;

      return this->InsertWithRebalancing(entityID, boxNew, boxes);
    }

    // Update id with rebalancing by the new bounding box information and the erase part is aided by the old bounding box geometry data
    bool Update(TEntityID entityID, TBox const& oldBox, TBox const& newBox, TContainer const& boxes) noexcept
    {
      if (!IGM::DoesRangeContainBoxAD(this->m_grid.GetBoxSpace(), newBox))
        return false;

      if constexpr (!DO_SPLIT_PARENT_ENTITIES)
        if (this->FindSmallestNode(oldBox) == this->FindSmallestNode(newBox))
          return true;

      if (!this->Erase<false>(entityID, oldBox))
        return false; // entityID was not registered previously.

      return this->InsertWithRebalancing(entityID, newBox, boxes);
    }


  private:
    void PickSearchRecursive(TVector const& pickPoint, TContainer const& boxes, MortonNodeIDCR parentKey, std::vector<TEntityID>& foundEntitiyIDs) const noexcept
    {
      auto const& parentNode = this->GetNode(parentKey);
      auto const& entities = this->GetNodeEntities(parentNode);
      std::copy_if(entities.begin(), entities.end(), std::back_inserter(foundEntitiyIDs), [&](auto const entityID) {
        return AD::DoesBoxContainPoint(detail::at(boxes, entityID), pickPoint);
      });

      auto const& centerPoint = GetNodeCenterMacro(this, parentKey, parentNode);
      bool isPickPointInCenter = true;
      bool isPickPointInCenterOngoing = true;
      for (MortonNodeIDCR keyChild : parentNode.GetChildren())
      {
        if (!isPickPointInCenter || isPickPointInCenterOngoing)
        {
          auto mask = MortonNodeID{ 1 };
          for (dim_t dimensionID = 0; dimensionID < DIMENSION_NO; ++dimensionID, mask <<= 1)
          {
            auto const lower = (keyChild & mask) == MortonNodeID{};
            auto const center = TGeometry(centerPoint[dimensionID]);
            if (lower)
            {
              if (center < AD::GetPointC(pickPoint, dimensionID))
                continue;
            }
            else
            {
              if (center > AD::GetPointC(pickPoint, dimensionID))
                continue;
            }

            if (isPickPointInCenter)
              isPickPointInCenter &= center == AD::GetPointC(pickPoint, dimensionID);
          }
        }
        isPickPointInCenterOngoing = false;

        PickSearchRecursive(pickPoint, boxes, keyChild, foundEntitiyIDs);
        if (!isPickPointInCenter)
          break;
      }
    }


  public: // Search functions
    // Pick search
    std::vector<TEntityID> PickSearch(TVector const& pickPoint, TContainer const& boxes) const noexcept
    {
      auto foundEntitiyIDs = std::vector<TEntityID>();
      if (!IGM::DoesBoxContainPointAD(this->m_grid.GetBoxSpace(), pickPoint))
        return foundEntitiyIDs;

      foundEntitiyIDs.reserve(100);

      auto const endIteratorOfNodes = this->m_nodes.end();
      auto const gridIDRange = this->m_grid.GetEdgePointGridID(pickPoint);
      auto rangeLocationID = SI::GetRangeLocationID(gridIDRange);

      auto nodeKey = SI::GetHash(this->m_maxDepthID, rangeLocationID[0]);
      if (rangeLocationID[0] != rangeLocationID[1]) // Pick point is on the nodes edge. It must check more nodes downward.
      {
        auto const rangeKey = SI::GetNodeID(this->m_maxDepthID, rangeLocationID);
        nodeKey = this->FindSmallestNodeKey(rangeKey);
        auto const nodeIterator = this->m_nodes.find(nodeKey);
        if (nodeIterator != endIteratorOfNodes)
          PickSearchRecursive(pickPoint, boxes, nodeIterator->first, foundEntitiyIDs);

        nodeKey = SI::GetParentKey(nodeKey);
      }

      for (; SI::IsValidKey(nodeKey); nodeKey = SI::GetParentKey(nodeKey))
      {
        auto const nodeIterator = this->m_nodes.find(nodeKey);
        if (nodeIterator == endIteratorOfNodes)
          continue;

        auto const& entities = this->GetNodeEntities(nodeIterator->second);
        std::copy_if(entities.begin(), entities.end(), std::back_inserter(foundEntitiyIDs), [&](auto const entityID) {
          return AD::DoesBoxContainPoint(detail::at(boxes, entityID), pickPoint);
        });
      }

      return foundEntitiyIDs;
    }

    // Range search
    template<bool DO_MUST_FULLY_CONTAIN = true>
    std::vector<TEntityID> RangeSearch(TBox const& range, TContainer const& boxes) const noexcept
    {
      auto foundEntities = std::vector<TEntityID>();
      this->template RangeSearchBaseRoot<DO_MUST_FULLY_CONTAIN, false>(range, boxes, foundEntities);

      if constexpr (DO_SPLIT_PARENT_ENTITIES)
        detail::sortAndUnique(foundEntities);

      return foundEntities;
    }

    // Hyperplane intersection (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> PlaneIntersection(
      TGeometry const& distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance, TContainer const& boxes) const noexcept
    {
      return this->PlaneIntersectionBase(distanceOfOrigo, planeNormal, tolerance, boxes);
    }

    // Hyperplane intersection using built-in plane
    inline std::vector<TEntityID> PlaneIntersection(TPlane const& plane, TGeometry tolerance, TContainer const& boxes) const noexcept
    {
      return this->PlaneIntersectionBase(AD::GetPlaneOrigoDistance(plane), AD::GetPlaneNormal(plane), tolerance, boxes);
    }

    // Hyperplane segmentation, get all elements in positive side (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> PlanePositiveSegmentation(
      TGeometry distanceOfOrigo, TVector const& planeNormal, TGeometry tolerance, TContainer const& boxes) const noexcept
    {
      return this->PlanePositiveSegmentationBase(distanceOfOrigo, planeNormal, tolerance, boxes);
    }

    // Hyperplane segmentation, get all elements in positive side (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> PlanePositiveSegmentation(TPlane const& plane, TGeometry tolerance, TContainer const& boxes) const noexcept
    {
      return this->PlanePositiveSegmentationBase(AD::GetPlaneOrigoDistance(plane), AD::GetPlaneNormal(plane), tolerance, boxes);
    }

    // Hyperplane segmentation, get all elements in positive side (Plane equation: dotProduct(planeNormal, point) = distanceOfOrigo)
    inline std::vector<TEntityID> FrustumCulling(std::span<TPlane const> const& boundaryPlanes, TGeometry tolerance, TContainer const& boxes) const noexcept
    {
      return this->FrustumCullingBase(boundaryPlanes, tolerance, boxes);
    }

  private:
    struct SweepAndPruneDatabase
    {
      inline constexpr SweepAndPruneDatabase(TContainer const& boxes, auto const& entityIDs) noexcept
      : m_sortedEntityIDs(entityIDs.begin(), entityIDs.end())
      {
        std::sort(m_sortedEntityIDs.begin(), m_sortedEntityIDs.end(), [&](auto const entityIDL, auto const entityIDR) {
          return AD::GetBoxMinC(detail::at(boxes, entityIDL), 0) < AD::GetBoxMinC(detail::at(boxes, entityIDR), 0);
        });
      }

      inline constexpr std::vector<TEntityID> const& GetEntities() const noexcept { return m_sortedEntityIDs; }

    private:
      std::vector<TEntityID> m_sortedEntityIDs;
    };

  public:
    // Client-defined Collision detector based on indices. AABB intersection is executed independently from this checker.
    using FCollisionDetector = std::function<bool(TEntityID, TEntityID)>;

    // Collision detection: Returns all overlapping boxes from the source trees.
    static std::vector<std::pair<TEntityID, TEntityID>> CollisionDetection(
      OrthoTreeBoundingBox const& leftTree, TContainer const& leftBoxes, OrthoTreeBoundingBox const& rightTree, TContainer const& rightBoxes) noexcept
    {
      using NodeIterator = typename Base::template NodeContainer<Node>::const_iterator;
      struct NodeIteratorAndStatus
      {
        NodeIterator Iterator;
        bool IsTraversed;
      };
      using ParentIteratorArray = std::array<NodeIteratorAndStatus, 2>;

      enum : bool
      {
        Left,
        Right
      };

      auto results = std::vector<std::pair<TEntityID, TEntityID>>{};
      results.reserve(leftBoxes.size() / 10);

      auto constexpr rootKey = SI::GetRootKey();
      auto const trees = std::array{ &leftTree, &rightTree };

      auto entitiesInOrderCache = std::array<std::unordered_map<MortonNodeID, SweepAndPruneDatabase>, 2>{};
      auto const getOrCreateEntitiesInOrder = [&](bool side, NodeIterator const& it, TContainer const& boxes) -> std::vector<TEntityID> const& {
        auto itKeyAndSPD = entitiesInOrderCache[side].find(it->first);
        if (itKeyAndSPD == entitiesInOrderCache[side].end())
        {
          bool isInserted = false;
          std::tie(itKeyAndSPD, isInserted) =
            entitiesInOrderCache[side].emplace(it->first, SweepAndPruneDatabase(boxes, trees[side]->GetNodeEntities(it->second)));
        }

        return itKeyAndSPD->second.GetEntities();
      };

      [[maybe_unused]] auto const pLeftTree = &leftTree;
      [[maybe_unused]] auto const pRightTree = &rightTree;
      auto nodePairToProceed = std::queue<ParentIteratorArray>{};
      nodePairToProceed.push({
        NodeIteratorAndStatus{  leftTree.m_nodes.find(rootKey), false },
        NodeIteratorAndStatus{ rightTree.m_nodes.find(rootKey), false }
      });
      for (; !nodePairToProceed.empty(); nodePairToProceed.pop())
      {
        auto const& parentNodePair = nodePairToProceed.front();

        // Check the current ascendant content

        auto const& leftEntitiesInOrder = getOrCreateEntitiesInOrder(Left, parentNodePair[Left].Iterator, leftBoxes);
        auto const& rightEntitiesInOrder = getOrCreateEntitiesInOrder(Right, parentNodePair[Right].Iterator, rightBoxes);

        auto const rightEntityNo = rightEntitiesInOrder.size();
        std::size_t iRightEntityBegin = 0;
        for (auto const leftEntityID : leftEntitiesInOrder)
        {
          for (; iRightEntityBegin < rightEntityNo; ++iRightEntityBegin)
            if (AD::GetBoxMaxC(detail::at(rightBoxes, rightEntitiesInOrder[iRightEntityBegin]), 0) >= AD::GetBoxMinC(detail::at(leftBoxes, leftEntityID), 0))
              break; // sweep and prune optimization

          for (std::size_t iRightEntity = iRightEntityBegin; iRightEntity < rightEntityNo; ++iRightEntity)
          {
            auto const rightEntityID = rightEntitiesInOrder[iRightEntity];

            if (AD::GetBoxMaxC(detail::at(leftBoxes, leftEntityID), 0) < AD::GetBoxMinC(detail::at(rightBoxes, rightEntityID), 0))
              break; // sweep and prune optimization

            if (AD::AreBoxesOverlapped(detail::at(leftBoxes, leftEntityID), detail::at(rightBoxes, rightEntityID), false))
              results.emplace_back(leftEntityID, rightEntityID);
          }
        }

        // Collect children
        auto childNodes = std::array<std::vector<NodeIteratorAndStatus>, 2>{};
        for (auto const sideID : { Left, Right })
        {
          auto const& [nodeIterator, fTraversed] = parentNodePair[sideID];
          if (fTraversed)
            continue;

          auto const& childIDs = nodeIterator->second.GetChildren();
          childNodes[sideID].resize(childIDs.size());
          std::transform(childIDs.begin(), childIDs.end(), childNodes[sideID].begin(), [&](MortonNodeIDCR childKey) -> NodeIteratorAndStatus {
            return { trees[sideID]->m_nodes.find(childKey), false };
          });
        }

        // Stop condition
        if (childNodes[Left].empty() && childNodes[Right].empty())
          continue;

        // Add parent if it has any element
        for (auto const sideID : { Left, Right })
          if (!trees[sideID]->IsNodeEntitiesEmpty(parentNodePair[sideID].Iterator->second))
            childNodes[sideID].push_back({ parentNodePair[sideID].Iterator, true });


        // Cartesian product of childNodes left and right
        for (auto const& leftChildNode : childNodes[Left])
          for (auto const& rightChildNode : childNodes[Right])
            if (!(leftChildNode.Iterator == parentNodePair[Left].Iterator && rightChildNode.Iterator == parentNodePair[Right].Iterator))
              if (IGM::AreBoxesOverlappingByCenter(
                    GetNodeCenterMacro(pLeftTree, leftChildNode.Iterator->first, leftChildNode.Iterator->second),
                    GetNodeCenterMacro(pRightTree, rightChildNode.Iterator->first, rightChildNode.Iterator->second),
                    leftTree.GetNodeSizeByKey(leftChildNode.Iterator->first),
                    rightTree.GetNodeSizeByKey(rightChildNode.Iterator->first)))
                nodePairToProceed.emplace(std::array{ leftChildNode, rightChildNode });
      }

      if constexpr (DO_SPLIT_PARENT_ENTITIES)
        detail::sortAndUnique(results);

      return results;
    }


    // Collision detection: Returns all overlapping boxes from the source trees.
    inline std::vector<std::pair<TEntityID, TEntityID>> CollisionDetection(
      TContainer const& boxes, OrthoTreeBoundingBox const& otherTree, TContainer const& otherBoxes) const noexcept
    {
      return CollisionDetection(*this, boxes, otherTree, otherBoxes);
    }

  private:
    struct NodeCollisionContext
    {
      IGM::Vector Center;
      IGM::Box Box;
      std::vector<TEntityID> EntityIDs;
    };

    constexpr void FillNodeCollisionContext(
      [[maybe_unused]] MortonNodeIDCR nodeKey, Node const& node, depth_t depthID, NodeCollisionContext& nodeContext) const noexcept
    {
      auto const& nodeEntities = this->GetNodeEntities(node);

      nodeContext.EntityIDs.clear();
      nodeContext.EntityIDs.assign(nodeEntities.begin(), nodeEntities.end());
      nodeContext.Center = GetNodeCenterMacro(this, nodeKey, node);
      nodeContext.Box = this->GetNodeBox(depthID, nodeContext.Center);
    }

    constexpr void PrepareNodeCollisionContext(
      TContainer const& boxes,
      auto const& comparator,
      depth_t depthID,
      NodeCollisionContext& nodeContext,
      NodeCollisionContext* parentNodeContext,
      std::vector<TEntityID>* splitEntities = nullptr) const noexcept
    {
      auto& entityIDs = nodeContext.EntityIDs;
      auto entityNo = entityIDs.size();

      // split entities are moved upwards in the hierarchy
      if constexpr (DO_SPLIT_PARENT_ENTITIES)
      {
        if (parentNodeContext)
        {
          auto& parentEntities = parentNodeContext->EntityIDs;

          auto const parentEntitiesWithoutSplitNo = parentEntities.size();
          for (std::size_t i = 0; i < entityNo; ++i)
          {
            auto entityID = entityIDs[i];
            auto const location = this->GetRangeLocationMetaData(detail::at(boxes, entityID));
            if (location.DepthID >= depthID)
              continue;

            parentEntities.emplace_back(entityID);
            if (splitEntities)
              splitEntities->emplace_back(entityID);

            --entityNo;
            entityIDs[i] = std::move(entityIDs[entityNo]);
            --i;
          }
          entityIDs.resize(entityNo);
          detail::inplaceMerge(comparator, parentEntities, parentEntitiesWithoutSplitNo);

          auto const uniqueEndIt = std::unique(parentEntities.begin(), parentEntities.end());
          parentEntities.resize(uniqueEndIt - parentEntities.begin());
        }
      }

      std::sort(entityIDs.begin(), entityIDs.end(), comparator);
    }

    constexpr void InsertCollidedEntitiesInsideNode(
      TContainer const& boxes,
      NodeCollisionContext const& context,
      std::vector<std::pair<TEntityID, TEntityID>>& collidedEntityPairs,
      std::optional<FCollisionDetector> const& collisionDetector) const noexcept
    {
      auto const& entityIDs = context.EntityIDs;
      auto const entityNo = entityIDs.size();
      for (std::size_t i = 0; i < entityNo; ++i)
      {
        auto const entityIDI = entityIDs[i];
        auto const& entityBoxI = detail::at(boxes, entityIDI);

        for (std::size_t j = i + 1; j < entityNo; ++j)
        {
          auto const entityIDJ = entityIDs[j];
          auto const& entityBoxJ = detail::at(boxes, entityIDJ);
          if (AD::GetBoxMaxC(entityBoxI, 0) < AD::GetBoxMinC(entityBoxJ, 0))
            break; // sweep and prune optimization

          if (AD::AreBoxesOverlappedStrict(entityBoxI, entityBoxJ))
            if (!collisionDetector || (*collisionDetector)(entityIDI, entityIDJ))
              collidedEntityPairs.emplace_back(entityIDI, entityIDJ);
        }
      }
    }

    constexpr void InsertCollidedEntitiesWithParents(
      TContainer const& boxes,
      depth_t depthID,
      std::vector<NodeCollisionContext> const& nodeContextStack,
      std::vector<std::pair<TEntityID, TEntityID>>& collidedEntityPairs,
      std::optional<FCollisionDetector> const& collisionDetector) const noexcept
    {
      auto const& nodeContext = nodeContextStack[depthID];
      auto const& nodeCenter = nodeContext.Center;
      auto const& nodeSizes = this->GetNodeSize(depthID);
      auto const& entityIDs = nodeContext.EntityIDs;

      auto const entityNo = entityIDs.size();

      for (depth_t parentDepthID = 0; parentDepthID < depthID; ++parentDepthID)
      {
        auto const& [parentCenter, parentBox, parentEntityIDs] = nodeContextStack[parentDepthID];

        auto iEntityBegin = std::size_t{};
        for (auto const parentEntityID : parentEntityIDs)
        {
          auto const& parentEntityBox = detail::at(boxes, parentEntityID);
          if (AD::GetBoxMinC(parentEntityBox, 0) > nodeContext.Box.Max[0])
            break;

          auto const parentEntityCenter = IGM::GetBoxCenterAD(parentEntityBox);
          auto const parentEntitySizes = IGM::GetBoxSizeAD(parentEntityBox);
          if (!IGM::AreBoxesOverlappingByCenter(nodeCenter, parentEntityCenter, nodeSizes, parentEntitySizes))
            continue;

          for (; iEntityBegin < entityNo; ++iEntityBegin)
            if (AD::GetBoxMaxC(detail::at(boxes, entityIDs[iEntityBegin]), 0) >= AD::GetBoxMinC(parentEntityBox, 0))
              break; // sweep and prune optimization

          for (std::size_t iEntity = iEntityBegin; iEntity < entityNo; ++iEntity)
          {
            auto const entityID = entityIDs[iEntity];
            auto const& entityBox = detail::at(boxes, entityID);

            if (AD::GetBoxMaxC(parentEntityBox, 0) < AD::GetBoxMinC(entityBox, 0))
              break; // sweep and prune optimization

            if (AD::AreBoxesOverlappedStrict(entityBox, parentEntityBox))
              if (!collisionDetector || (*collisionDetector)(entityID, parentEntityID))
                collidedEntityPairs.emplace_back(entityID, parentEntityID);
          }
        }
      }
    }

    void InsertCollidedEntitiesInSubtree(
      TContainer const& boxes,
      auto const& comparator,
      depth_t depthID,
      MortonNodeIDCR nodeKey,
      std::vector<NodeCollisionContext>& nodeContextStack,
      std::vector<std::pair<TEntityID, TEntityID>>& collidedEntityPairs,
      std::optional<FCollisionDetector> const& collisionDetector,
      std::vector<TEntityID>* splitEntities = nullptr) const noexcept
    {
      auto const& node = this->GetNode(nodeKey);

      FillNodeCollisionContext(nodeKey, node, depthID, nodeContextStack[depthID]);
      PrepareNodeCollisionContext(boxes, comparator, depthID, nodeContextStack[depthID], depthID == 0 ? nullptr : &nodeContextStack[depthID - 1], splitEntities);

      auto const childDepthID = depthID + 1;
      for (MortonLocationIDCR childKey : node.GetChildren())
        InsertCollidedEntitiesInSubtree(boxes, comparator, childDepthID, childKey, nodeContextStack, collidedEntityPairs, collisionDetector);

      InsertCollidedEntitiesInsideNode(boxes, nodeContextStack[depthID], collidedEntityPairs, collisionDetector);
      InsertCollidedEntitiesWithParents(boxes, depthID, nodeContextStack, collidedEntityPairs, collisionDetector);
    }

    // Collision detection between the stored elements from bottom to top logic
    template<bool IS_PARALLEL_EXEC = false>
    std::vector<std::pair<TEntityID, TEntityID>> CollectCollidedEntities(
      TContainer const& boxes, std::optional<FCollisionDetector> const& collisionDetector = std::nullopt) const noexcept
    {
      auto const comparator = [&boxes](TEntityID entityID1, TEntityID entityID2) {
        auto const x1 = AD::GetBoxMinC(detail::at(boxes, entityID1), 0);
        auto const x2 = AD::GetBoxMinC(detail::at(boxes, entityID2), 0);
        return x1 < x2 || (x1 == x2 && entityID1 < entityID2);
      };

      auto const entityNo = boxes.size();
      auto collidedEntityPairs = std::vector<std::pair<TEntityID, TEntityID>>{};
      collidedEntityPairs.reserve(std::max<std::size_t>(100, entityNo / 10));
      if constexpr (!IS_PARALLEL_EXEC)
      {
        auto nodeContextStack = std::vector<NodeCollisionContext>(this->GetDepthNo());
        this->InsertCollidedEntitiesInSubtree(boxes, comparator, 0, SI::GetRootKey(), nodeContextStack, collidedEntityPairs, collisionDetector);
      }
      else
      {
        auto const nodeNo = this->m_nodes.size();
        auto const threadNo = std::size_t(std::thread::hardware_concurrency());
        auto const isSingleThreadMoreEffective = nodeNo < threadNo * 3;
        if (isSingleThreadMoreEffective)
        {
          auto nodeContextStack = std::vector<NodeCollisionContext>(this->GetDepthNo());
          this->InsertCollidedEntitiesInSubtree(boxes, comparator, 0, SI::GetRootKey(), nodeContextStack, collidedEntityPairs, collisionDetector);
        }
        else
        {
          using NodeIterator = typename Base::template NodeContainer<Node>::const_iterator;

          auto nodeQueue = std::vector<NodeIterator>{};
          nodeQueue.reserve(threadNo * 2);
          nodeQueue.emplace_back(this->m_nodes.find(SI::GetRootKey()));

          auto nodeContextMap = std::unordered_map<MortonNodeID, NodeCollisionContext>{};

          std::size_t nodeQueueNo = 1;
          for (std::size_t i = 0; 0 < nodeQueueNo && nodeQueueNo < threadNo - 2; --nodeQueueNo, ++i)
          {
            for (MortonLocationIDCR childKey : nodeQueue[i]->second.GetChildren())
            {
              nodeQueue.emplace_back(this->m_nodes.find(childKey));
              ++nodeQueueNo;
            }

            MortonNodeIDCR nodeKey = nodeQueue[i]->first;
            auto const depthID = SI::GetDepthID(nodeKey);
            auto const parentKey = SI::GetParentKey(nodeKey);
            auto& nodeContext = nodeContextMap[nodeKey];
            FillNodeCollisionContext(nodeKey, this->GetNode(nodeKey), depthID, nodeContext);
            PrepareNodeCollisionContext(boxes, comparator, depthID, nodeContext, i == 0 ? nullptr : &nodeContextMap[parentKey]);
          }

          if (nodeQueueNo == 0)
          {
            auto nodeContextStack = std::vector<NodeCollisionContext>(this->GetDepthNo());
            InsertCollidedEntitiesInSubtree(boxes, comparator, 0, SI::GetRootKey(), nodeContextStack, collidedEntityPairs, collisionDetector);
          }
          else
          {
            struct TaskContext
            {
              NodeIterator NodeIt;
              std::vector<std::pair<TEntityID, TEntityID>> CollidedEntityPairs;
              std::vector<TEntityID> SplitEntities;
            };

            auto const nodeQueueAllNo = nodeQueue.size();
            auto const nodeQueueBegin = nodeQueueAllNo - nodeQueueNo;
            auto taskContexts = std::vector<TaskContext>(nodeQueueNo);
            for (std::size_t taskID = 0; taskID < nodeQueueNo; ++taskID)
              taskContexts[taskID].NodeIt = nodeQueue[nodeQueueBegin + taskID];

            EXEC_POL_DEF(epcd); // GCC 11.3
            std::for_each(EXEC_POL_ADD(epcd) taskContexts.begin(), taskContexts.end(), [&](auto& taskContext) {
              auto const depthID = SI::GetDepthID(taskContext.NodeIt->first);
              auto parentDepthID = depthID;

              auto nodeContextStack = std::vector<NodeCollisionContext>(this->GetDepthNo());
              for (auto parentKey = SI::GetParentKey(taskContext.NodeIt->first); SI::IsValidKey(parentKey); parentKey = SI::GetParentKey(parentKey))
                nodeContextStack[--parentDepthID] = nodeContextMap.at(parentKey);

              InsertCollidedEntitiesInSubtree(
                boxes,
                comparator,
                depthID,
                taskContext.NodeIt->first,
                nodeContextStack,
                taskContext.CollidedEntityPairs,
                collisionDetector,
                &taskContext.SplitEntities);
            });

            if constexpr (DO_SPLIT_PARENT_ENTITIES)
            {
              auto splitEntities = std::unordered_map<MortonNodeID, std::unordered_set<TEntityID>>{};
              for (auto const& taskContext : taskContexts)
              {
                auto const parentKey = SI::GetParentKey(taskContext.NodeIt->first);
                splitEntities[parentKey].insert(taskContext.SplitEntities.begin(), taskContext.SplitEntities.end());
              }

              for (auto const& [nodeKey, splitEntitiesSet] : splitEntities)
              {
                auto& entityIDs = nodeContextMap.at(nodeKey).EntityIDs;
                auto const parentEntitiesWithoutSplitNo = entityIDs.size();
                entityIDs.insert(entityIDs.end(), splitEntitiesSet.begin(), splitEntitiesSet.end());

                detail::inplaceMerge(comparator, entityIDs, parentEntitiesWithoutSplitNo);
                auto const uniqueEndIt = std::unique(entityIDs.begin(), entityIDs.end());
                entityIDs.resize(uniqueEndIt - entityIDs.begin());
              }
            }

            auto collidedEntityPairsInParents = std::vector<std::pair<TEntityID, TEntityID>>{};
            auto nodeContextStack = std::vector<NodeCollisionContext>();
            auto usedContextsStack = std::vector<NodeCollisionContext*>{};
            std::for_each(nodeQueue.begin(), nodeQueue.end() - nodeQueueNo, [&](auto& nodeIt) {
              {
                usedContextsStack.emplace_back(&nodeContextMap.at(nodeIt->first));
                for (auto parentKey = SI::GetParentKey(nodeIt->first); SI::IsValidKey(parentKey); parentKey = SI::GetParentKey(parentKey))
                  usedContextsStack.emplace_back(&nodeContextMap.at(parentKey));

                for (auto it = usedContextsStack.rbegin(); it != usedContextsStack.rend(); ++it)
                  nodeContextStack.emplace_back(std::move(*(*it)));
              }

              auto const depthID = depth_t(usedContextsStack.size()) - 1;
              InsertCollidedEntitiesInsideNode(boxes, nodeContextStack[depthID], collidedEntityPairsInParents, collisionDetector);
              InsertCollidedEntitiesWithParents(boxes, depthID, nodeContextStack, collidedEntityPairsInParents, collisionDetector);

              {
                auto i = 0;
                for (auto it = usedContextsStack.rbegin(); it != usedContextsStack.rend(); ++it, ++i)
                  *(*it) = std::move(nodeContextStack[i]);
                usedContextsStack.clear();
                nodeContextStack.clear();
              }
            });

            auto const collisionNo =
              std::transform_reduce(taskContexts.begin(), taskContexts.end(), collidedEntityPairsInParents.size(), std::plus{}, [](auto const& taskContext) {
                return taskContext.CollidedEntityPairs.size();
              });

            collidedEntityPairs.reserve(collisionNo);
            collidedEntityPairs.insert(collidedEntityPairs.end(), collidedEntityPairsInParents.begin(), collidedEntityPairsInParents.end());
            for (auto const& taskContext : taskContexts)
              collidedEntityPairs.insert(collidedEntityPairs.end(), taskContext.CollidedEntityPairs.begin(), taskContext.CollidedEntityPairs.end());
          }
        }
      }

      return collidedEntityPairs;
    }

  public:
    // Collision detection between the stored elements from bottom to top logic
    template<bool IS_PARALLEL_EXEC = false>
    inline std::vector<std::pair<TEntityID, TEntityID>> CollisionDetection(TContainer const& boxes) const noexcept
    {
      return CollectCollidedEntities<IS_PARALLEL_EXEC>(boxes, std::nullopt);
    }

    // Collision detection between the stored elements from bottom to top logic
    template<bool IS_PARALLEL_EXEC = false>
    inline std::vector<std::pair<TEntityID, TEntityID>> CollisionDetection(TContainer const& boxes, FCollisionDetector&& collisionDetector) const noexcept
    {
      return CollectCollidedEntities<IS_PARALLEL_EXEC>(boxes, collisionDetector);
    }

  private:
    void GetRayIntersectedAllRecursive(
      depth_t depthID,
      MortonNodeIDCR parentKey,
      TContainer const& boxes,
      TVector const& rayBasePoint,
      TVector const& rayHeading,
      TGeometry tolerance,
      TGeometry maxExaminationDistance,
      std::vector<EntityDistance>& foundEntities) const noexcept
    {
      auto const& node = this->GetNode(parentKey);

      auto const isNodeHit =
        IGM::GetRayBoxDistanceAD(GetNodeCenterMacro(this, parentKey, node), this->GetNodeSize(depthID + 1), rayBasePoint, rayHeading, tolerance);
      if (!isNodeHit)
        return;

      for (auto const entityID : this->GetNodeEntities(node))
      {
        auto const entityDistance = AD::GetRayBoxDistance(detail::at(boxes, entityID), rayBasePoint, rayHeading, tolerance);
        if (entityDistance && (maxExaminationDistance == 0 || entityDistance.value() <= maxExaminationDistance))
          foundEntities.push_back({ { IGM_Geometry(entityDistance.value()) }, entityID });
      }

      ++depthID;
      for (MortonNodeIDCR childKey : node.GetChildren())
        GetRayIntersectedAllRecursive(depthID, childKey, boxes, rayBasePoint, rayHeading, tolerance, maxExaminationDistance, foundEntities);
    }


    void GetRayIntersectedFirstRecursive(
      depth_t depthID,
      Node const& parentNode,
      TContainer const& boxes,
      TVector const& rayBasePoint,
      TVector const& rayHeading,
      TGeometry tolerance,
      std::optional<EntityDistance>& foundEntity) const noexcept
    {
      for (auto const entityID : this->GetNodeEntities(parentNode))
      {
        auto const distance = AD::GetRayBoxDistance(detail::at(boxes, entityID), rayBasePoint, rayHeading, tolerance);
        if (!distance)
          continue;

        if (!foundEntity || foundEntity->Distance > *distance)
          foundEntity = std::optional<EntityDistance>(std::in_place, EntityDistance{ { TGeometry(*distance) }, entityID });
      }

      ++depthID;
      auto const& halfSize = this->GetNodeSize(depthID + 1);
      auto nodeDistances = std::vector<BoxDistance>();
      for (MortonNodeIDCR childKey : parentNode.GetChildren())
      {
        auto const& nodeChild = this->GetNode(childKey);
        auto const distance = IGM::GetRayBoxDistanceAD(GetNodeCenterMacro(this, childKey, nodeChild), halfSize, rayBasePoint, rayHeading, tolerance);
        if (!distance)
          continue;

        if (foundEntity && *distance > foundEntity->Distance)
          continue;

        nodeDistances.push_back({ { IGM_Geometry(distance.value()) }, childKey, &nodeChild });
      }

      std::sort(nodeDistances.begin(), nodeDistances.end());

      for (auto const& nodeDistance : nodeDistances)
      {
        if (foundEntity && nodeDistance.Distance - tolerance >= foundEntity->Distance)
          break;

        GetRayIntersectedFirstRecursive(depthID, *nodeDistance.NodePtr, boxes, rayBasePoint, rayHeading, tolerance, foundEntity);
      }
    }


  public:
    // Get all box which is intersected by the ray in order
    std::vector<TEntityID> RayIntersectedAll(
      TVector const& rayBasePointPoint,
      TVector const& rayHeading,
      TContainer const& boxes,
      TGeometry tolerance = {},
      TGeometry maxExaminationDistance = {}) const noexcept
    {
      auto foundEntities = std::vector<EntityDistance>();
      foundEntities.reserve(20);
      GetRayIntersectedAllRecursive(0, SI::GetRootKey(), boxes, rayBasePointPoint, rayHeading, tolerance, maxExaminationDistance, foundEntities);

      auto const beginIteratorOfEntities = foundEntities.begin();
      auto endIteratorOfEntities = foundEntities.end();
      std::sort(beginIteratorOfEntities, endIteratorOfEntities);
      if constexpr (DO_SPLIT_PARENT_ENTITIES)
        endIteratorOfEntities =
          std::unique(beginIteratorOfEntities, endIteratorOfEntities, [](auto const& lhs, auto const& rhs) { return lhs.EntityID == rhs.EntityID; });

      auto foundEntityIDs = std::vector<TEntityID>(std::distance(beginIteratorOfEntities, endIteratorOfEntities));
      std::transform(beginIteratorOfEntities, endIteratorOfEntities, foundEntityIDs.begin(), [](auto const& entityDistance) {
        return entityDistance.EntityID;
      });
      return foundEntityIDs;
    }

    // Get all box which is intersected by the ray in order
    inline std::vector<TEntityID> RayIntersectedAll(
      TRay const& ray, TContainer const& boxes, TGeometry tolerance = {}, TGeometry maxExaminationDistance = {}) const noexcept
    {
      return RayIntersectedAll(ray.Origin, ray.Direction, boxes, tolerance, maxExaminationDistance);
    }

    // Get first box which is intersected by the ray
    std::optional<TEntityID> RayIntersectedFirst(
      TVector const& rayBasePoint, TVector const& rayHeading, TContainer const& boxes, TGeometry tolerance = {}) const noexcept
    {
      auto const& rootNode = this->GetNode(SI::GetRootKey());
      auto const distance =
        IGM::GetRayBoxDistanceAD(GetNodeCenterMacro(this, SI::GetRootKey(), rootNode), this->GetNodeSize(1), rayBasePoint, rayHeading, tolerance);
      if (!distance)
        return std::nullopt;

      auto foundEntity = std::optional<EntityDistance>{};
      GetRayIntersectedFirstRecursive(0, rootNode, boxes, rayBasePoint, rayHeading, tolerance, foundEntity);
      if (!foundEntity)
        return std::nullopt;

      return foundEntity->EntityID;
    }

    // Get first box which is intersected by the ray
    inline std::optional<TEntityID> RayIntersectedFirst(TRay const& ray, TContainer const& boxes, TGeometry tolerance = {}) const noexcept
    {
      return RayIntersectedFirst(ray.Origin, ray.Direction, boxes, tolerance);
    }
  };


  template<dim_t DIMENSION_NO, typename TGeometry = double>
  using VectorND = std::array<TGeometry, DIMENSION_NO>;

  template<dim_t DIMENSION_NO, typename TGeometry = double>
  using PointND = VectorND<DIMENSION_NO, TGeometry>;

  template<dim_t DIMENSION_NO, typename TGeometry = double>
  struct BoundingBoxND
  {
    VectorND<DIMENSION_NO, TGeometry> Min;
    VectorND<DIMENSION_NO, TGeometry> Max;
  };

  template<dim_t DIMENSION_NO, typename TGeometry = double>
  struct RayND
  {
    VectorND<DIMENSION_NO, TGeometry> Origin;
    VectorND<DIMENSION_NO, TGeometry> Direction;
  };

  template<dim_t DIMENSION_NO, typename TGeometry = double>
  struct PlaneND
  {
    TGeometry OrigoDistance;
    VectorND<DIMENSION_NO, TGeometry> Normal;
  };


  // Aliases
  using BaseGeometryType = double;
  using Vector1D = OrthoTree::VectorND<1, BaseGeometryType>;
  using Vector2D = OrthoTree::VectorND<2, BaseGeometryType>;
  using Vector3D = OrthoTree::VectorND<3, BaseGeometryType>;
  using Point1D = OrthoTree::PointND<1, BaseGeometryType>;
  using Point2D = OrthoTree::PointND<2, BaseGeometryType>;
  using Point3D = OrthoTree::PointND<3, BaseGeometryType>;
  using BoundingBox1D = OrthoTree::BoundingBoxND<1, BaseGeometryType>;
  using BoundingBox2D = OrthoTree::BoundingBoxND<2, BaseGeometryType>;
  using BoundingBox3D = OrthoTree::BoundingBoxND<3, BaseGeometryType>;
  using Ray2D = OrthoTree::RayND<2, BaseGeometryType>;
  using Ray3D = OrthoTree::RayND<3, BaseGeometryType>;
  using Plane2D = OrthoTree::PlaneND<2, BaseGeometryType>;
  using Plane3D = OrthoTree::PlaneND<3, BaseGeometryType>;

  template<dim_t DIMENSION_NO, typename TGeometry = BaseGeometryType>
  using AdaptorGeneralND = AdaptorGeneral<
    DIMENSION_NO,
    OrthoTree::VectorND<DIMENSION_NO, TGeometry>,
    OrthoTree::BoundingBoxND<DIMENSION_NO, TGeometry>,
    OrthoTree::RayND<DIMENSION_NO, TGeometry>,
    OrthoTree::PlaneND<DIMENSION_NO, TGeometry>,
    TGeometry>;

  template<dim_t DIMENSION_NO, typename TGeometry = BaseGeometryType, typename TContainer = std::span<OrthoTree::VectorND<DIMENSION_NO, TGeometry> const>>
  using TreePointND = OrthoTree::OrthoTreePoint<
    DIMENSION_NO,
    OrthoTree::VectorND<DIMENSION_NO, TGeometry>,
    OrthoTree::BoundingBoxND<DIMENSION_NO, TGeometry>,
    OrthoTree::RayND<DIMENSION_NO, TGeometry>,
    OrthoTree::PlaneND<DIMENSION_NO, TGeometry>,
    TGeometry,
    AdaptorGeneralND<DIMENSION_NO, TGeometry>,
    TContainer>;

  template<
    dim_t DIMENSION_NO,
    bool DO_SPLIT_PARENT_ENTITIES = true,
    typename TGeometry = BaseGeometryType,
    typename TContainer = std::span<OrthoTree::BoundingBoxND<DIMENSION_NO, TGeometry> const>>
  using TreeBoxND = OrthoTree::OrthoTreeBoundingBox<
    DIMENSION_NO,
    OrthoTree::VectorND<DIMENSION_NO, TGeometry>,
    OrthoTree::BoundingBoxND<DIMENSION_NO, TGeometry>,
    OrthoTree::RayND<DIMENSION_NO, TGeometry>,
    OrthoTree::PlaneND<DIMENSION_NO, TGeometry>,
    TGeometry,
    DO_SPLIT_PARENT_ENTITIES,
    AdaptorGeneralND<DIMENSION_NO, TGeometry>,
    TContainer>;

  // Dualtree for points
  using DualtreePoint = TreePointND<1, BaseGeometryType>;

  // Dualtree for bounding boxes
  using DualtreeBox = TreeBoxND<1, true, BaseGeometryType>;

  // Quadtree for points
  using QuadtreePoint = TreePointND<2, BaseGeometryType>;

  // Quadtree for bounding boxes
  using QuadtreeBox = TreeBoxND<2, true, BaseGeometryType>;

  // Octree for points
  using OctreePoint = TreePointND<3, BaseGeometryType>;

  // Octree for bounding boxes
  using OctreeBox = TreeBoxND<3, true, BaseGeometryType>;

  // Hexatree for points
  using HexatreePoint = TreePointND<4, BaseGeometryType>;

  // Hexatree for bounding boxes
  using HexatreeBox = TreeBoxND<4, true, BaseGeometryType>;

  // OrthoTrees for higher dimensions
  using TreePoint16D = TreePointND<16, BaseGeometryType>;
  using TreeBox16D = TreeBoxND<16, true, BaseGeometryType>;


  // Dualtree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES>
  using DualtreeBoxs = TreeBoxND<1, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType>;

  // Quadtree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES>
  using QuadtreeBoxs = TreeBoxND<2, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType>;

  // Octree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES>
  using OctreeBoxs = TreeBoxND<3, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType>;

  // Hexatree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES>
  using HexatreeBoxs = TreeBoxND<4, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType>;

  // OrthoTrees for higher dimensions with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES>
  using TreeBox16Ds = TreeBoxND<16, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType>;


  // OrthoTrees with std::unordered_map

  // std::unordered_map-based Quadtree for points
  using QuadtreePointMap = TreePointND<2, BaseGeometryType, std::unordered_map<index_t, OrthoTree::Vector2D>>;

  // std::unordered_map-based Octree for points
  using OctreePointMap = TreePointND<3, BaseGeometryType, std::unordered_map<index_t, OrthoTree::Vector3D>>;

  // std::unordered_map-based Octree for bounding boxes
  using QuadreeBoxMap = TreeBoxND<2, true, BaseGeometryType, std::unordered_map<index_t, OrthoTree::BoundingBox2D>>;

  // std::unordered_map-based Octree for bounding boxes
  using OctreeBoxMap = TreeBoxND<3, true, BaseGeometryType, std::unordered_map<index_t, OrthoTree::BoundingBox3D>>;

  // std::unordered_map-based Quadtree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES>
  using QuadtreeBoxsMap = TreeBoxND<2, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType, std::unordered_map<index_t, OrthoTree::BoundingBox2D>>;
  using QuadtreeBoxMap = TreeBoxND<2, true, BaseGeometryType, std::unordered_map<index_t, OrthoTree::BoundingBox2D>>;

  // std::unordered_map-based Octree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES>
  using OctreeBoxsMap = TreeBoxND<3, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType, std::unordered_map<index_t, OrthoTree::BoundingBox3D>>;
  using OctreeBoxMap = TreeBoxND<3, true, BaseGeometryType, std::unordered_map<index_t, OrthoTree::BoundingBox3D>>;


  // User-defined container-based Quadtree for points
  template<typename UDMap>
  using QuadtreePointUDMap = TreePointND<2, BaseGeometryType, UDMap>;

  // User-defined container-based Octree for points
  template<typename UDMap>
  using OctreePointUDMap = TreePointND<3, BaseGeometryType, UDMap>;

  // User-defined container-based Octree for bounding boxes
  template<typename UDMap>
  using QuadreeBoxUDMap = TreeBoxND<2, true, BaseGeometryType, UDMap>;

  // User-defined container-based Octree for bounding boxes
  template<typename UDMap>
  using OctreeBoxUDMap = TreeBoxND<3, true, BaseGeometryType, UDMap>;

  // User-defined container-based Quadtree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES, typename UDMap>
  using QuadtreeBoxsUDMap = TreeBoxND<2, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType, UDMap>;

  // User-defined container-based Octree for bounding boxes with split-depth settings
  template<bool DO_SPLIT_PARENT_ENTITIES, typename UDMap>
  using OctreeBoxsUDMap = TreeBoxND<3, DO_SPLIT_PARENT_ENTITIES, BaseGeometryType, UDMap>;
} // namespace OrthoTree


#include "octree_container.h"

#undef LOOPIVDEP
#ifdef CRASH_UNDEF
#undef CRASH_UNDEF
#undef CRASH
#endif // CRASH_UNDEF
#ifdef CRASH_IF_UNDEF
#undef CRASH_IF_UNDEF
#undef CRASH_IF
#endif // CRASH_IF_UNDEF

#endif // ORTHOTREE_GUARD
