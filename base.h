#ifndef BASE_H
#define BASE_H

#ifdef __cplusplus
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#else
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#endif

// ---------------------- Типы ----------------------
typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef size_t usize;
typedef ptrdiff_t isize;

// ---------------------- Компилятор ----------------------
#if defined(__clang__)
#define COMPILER_CLANG 1
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#elif defined(_MSC_VER)
#define COMPILER_MSVC 1
#else
#define COMPILER_UNKNOWN 1
#endif

// ---------------------- Inline / Force inline ----------------------
#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE inline
#endif
#define INLINE inline

// ---------------------- Noreturn ----------------------
#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

// ---------------------- Deprecated ----------------------
#ifdef _MSC_VER
#define DEPRECATED(msg) __declspec(deprecated(msg))
#elif defined(__GNUC__) || defined(__clang__)
#define DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#define DEPRECATED(msg)
#endif

// ---------------------- Align ----------------------
#ifdef _MSC_VER
#define ALIGN(N) __declspec(align(N))
#elif defined(__GNUC__) || defined(__clang__)
#define ALIGN(N) __attribute__((aligned(N)))
#else
#define ALIGN(N)
#endif

// ---------------------- Likely / Unlikely ----------------------
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

// ---------------------- C++ проверка ----------------------
#ifdef __cplusplus
#define CPP_VERSION __cplusplus
#else
#define CPP_VERSION 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------- Отдельная функция для ошибок ----------------------
NORETURN inline void fatal_alloc_error(usize size) {
    fprintf(stderr, "aligned_malloc failed for size %zu\n", size);
    exit(1);
}

// ---------------------- Выделение выровненной памяти ----------------------
FORCE_INLINE void *aligned_malloc(usize size, usize alignment) {
    void *ptr = NULL;

#if defined(_WIN32) || defined(_WIN64) || defined(COMPILER_MSVC)
    // В Windows (и для MSVC, и для Clang-cl) используем это:
    ptr = _aligned_malloc(size, alignment);
#elif defined(__linux__) || defined(__APPLE__)
    // В POSIX системах используем posix_memalign:
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = NULL;
    }
#else
    // Запасной вариант для остальных систем
    ptr = malloc(size);
#endif

    if (UNLIKELY(ptr == NULL))
        fatal_alloc_error(size);

    return ptr;
}

// ---------------------- Освобождение выровненной памяти ----------------------
FORCE_INLINE void aligned_free(void *ptr) {
    if (ptr == NULL)
        return;

#if defined(COMPILER_MSVC)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// ---------------------- Concept Allocator

// Проверяем метод alloc(std::size_t) -> void*
template <typename T>
concept Allocatable = requires(T t, std::size_t n) {
    { t.alloc(n) } -> std::same_as<void *>;
};

// Проверяем метод dealloc(void*) -> void
template <typename T>
concept Deallocatable = requires(T t, void *p) {
    { t.dealloc(p) } -> std::same_as<void>;
};

// Проверяем метод reset() -> void
template <typename T>
concept Resetable = requires(T t) {
    { t.reset() } -> std::same_as<void>;
};

// Проверяем метод calloc(std::size_t, std::size_t) -> void*
template <typename T>
concept Calocable = requires(T t, std::size_t count, std::size_t size) {
    { t.calloc(count, size) } -> std::same_as<void *>;
};

// ---------------------- С++ Default Allocator -----------------------

// Функция для alloc
template <Allocatable T> void *alloc(T &allocator, std::size_t n) {
    return allocator.alloc(n);
}

// Функция для dealloc
template <Deallocatable T> void dealloc(T &allocator, void *ptr) {
    allocator.dealloc(ptr);
}

// Функция для reset
template <Resetable T> void reset(T &allocator) { allocator.reset(); }

// Функция для calloc
template <Calocable T>
void *alloc_zeroed(T &allocator, std::size_t count, std::size_t size) {
    return allocator.calloc(count, size);
}

// ---------------------- C++ Arena -----------------------------

struct ArenaAllocator {
    u8 *buffer;
    usize capacity;
    usize offset;

    ArenaAllocator(usize size) : capacity(size), offset(0) {
        buffer =
            static_cast<u8 *>(aligned_malloc(size, alignof(std::max_align_t)));
    }

    ~ArenaAllocator() { aligned_free(buffer); }

    void *alloc(usize n, usize alignment = alignof(std::max_align_t)) {
        usize current = reinterpret_cast<usize>(buffer + offset);
        usize aligned = (current + alignment - 1) & ~(alignment - 1);

        usize new_offset = aligned - reinterpret_cast<usize>(buffer) + n;

        if (new_offset > capacity)
            return nullptr;

        offset = new_offset;
        return reinterpret_cast<void *>(aligned);
    }

    void reset() { offset = 0; }

    void *calloc(usize count, usize size) {
        if (size != 0 && count > SIZE_MAX / size)
            return nullptr;

        usize total = count * size;
        void *ptr = alloc(total);

        if (ptr) {
            std::memset(ptr, 0, total);
        }
        return ptr;
    }
};

// ---------------------- C++ Pool -----------------------------

struct PoolAllocator {
    struct Block {
        Block *next;
    };

    u8 *buffer;
    Block *free_list;
    usize block_size;
    usize capacity;

    PoolAllocator(usize count, usize size)
        : block_size(size < sizeof(Block) ? sizeof(Block) : size),
          capacity(count) {
        buffer =
            (u8 *)aligned_malloc(block_size * count, alignof(std::max_align_t));

        // строим free list
        free_list = nullptr;

        for (usize i = 0; i < count; ++i) {
            Block *block = (Block *)(buffer + i * block_size);
            block->next = free_list;
            free_list = block;
        }
    }

    ~PoolAllocator() { aligned_free(buffer); }

    void *alloc() {
        if (!free_list)
            return nullptr;

        Block *b = free_list;
        free_list = b->next;
        return b;
    }

    void dealloc(void *ptr) {
        Block *b = (Block *)ptr;
        b->next = free_list;
        free_list = b;
    }

    void reset() {
        free_list = nullptr;

        for (usize i = 0; i < capacity; ++i) {
            Block *block = (Block *)(buffer + i * block_size);
            block->next = free_list;
            free_list = block;
        }
    }
};

template <typename T, typename... Args>
T* pool_create(PoolAllocator& pool, Args&&... args) {
    void* mem = pool.alloc();
    if (!mem) return nullptr;
    return new (mem) T(std::forward<Args>(args)...);
}

template <typename T>
void pool_destroy(PoolAllocator& pool, T* obj) {
    if (!obj) return;
    obj->~T();
    pool.dealloc(obj);
}

// ---------------------- C++ шаблон для типизированных массивов
template <typename T>
FORCE_INLINE T *aligned_malloc_array(usize count, usize alignment) {
    return static_cast<T *>(aligned_malloc(sizeof(T) * count, alignment));
}

template <typename T> FORCE_INLINE void aligned_free_array(T *ptr) {
    aligned_free(static_cast<void *>(ptr));
}

#endif

#endif // BASE_H