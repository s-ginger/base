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

// ---------------------- Потоки, Mutex, Once, Barrier ----------------------
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#endif

// -------- Потоки --------
typedef
#ifdef _WIN32
    HANDLE
#else
    pthread_t
#endif
        Thread;

typedef void *(*ThreadFunc)(void *);

FORCE_INLINE int thread_create(Thread *t, ThreadFunc func, void *arg) {
#ifdef _WIN32
    *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return *t == NULL ? -1 : 0;
#else
    return pthread_create(t, NULL, func, arg);
#endif
}

FORCE_INLINE int thread_join(Thread t) {
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 0;
#else
    return pthread_join(t, NULL);
#endif
}

// ---------------------- Once wrapper ----------------------
#ifdef _WIN32
#include <windows.h>

typedef INIT_ONCE Once;
#define ONCE_INIT INIT_ONCE_STATIC_INIT

// Обёртка, чтобы можно было передавать void func(void)
typedef void (*once_func_t)(void);

static BOOL CALLBACK once_trampoline(PINIT_ONCE InitOnce, PVOID Parameter,
                                     PVOID *Context) {
    once_func_t func = (once_func_t)Parameter;
    func();
    return TRUE;
}

static FORCE_INLINE int once_execute(Once *once, once_func_t func) {
    return InitOnceExecuteOnce(once, once_trampoline, (PVOID)func, NULL) ? 0
                                                                         : -1;
}

#else
#include <pthread.h>

typedef pthread_once_t Once;
#define ONCE_INIT PTHREAD_ONCE_INIT

FORCE_INLINE int once_execute(Once *once, void (*func)(void)) {
    return pthread_once(once, func);
}

#endif

// -------- Barrier --------
#ifdef _WIN32
typedef struct {
    int count;
    int waiting;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
} Barrier;

FORCE_INLINE int barrier_init(Barrier *b, int count) {
    b->count = count;
    b->waiting = 0;
    InitializeCriticalSection(&b->cs);
    InitializeConditionVariable(&b->cv);
    return 0;
}

FORCE_INLINE int barrier_wait(Barrier *b) {
    EnterCriticalSection(&b->cs);
    b->waiting++;
    if (b->waiting >= b->count) {
        b->waiting = 0;
        WakeAllConditionVariable(&b->cv);
    } else {
        SleepConditionVariableCS(&b->cv, &b->cs, INFINITE);
    }
    LeaveCriticalSection(&b->cs);
    return 0;
}

FORCE_INLINE int barrier_destroy(Barrier *b) {
    DeleteCriticalSection(&b->cs);
    return 0;
}

#else
typedef pthread_barrier_t Barrier;

FORCE_INLINE int barrier_init(Barrier *b, int count) {
    return pthread_barrier_init(b, NULL, count);
}

FORCE_INLINE int barrier_wait(Barrier *b) { return pthread_barrier_wait(b); }

FORCE_INLINE int barrier_destroy(Barrier *b) {
    return pthread_barrier_destroy(b);
}
#endif

// -------- Mutex --------
#ifdef _WIN32
typedef CRITICAL_SECTION Mutex;
FORCE_INLINE void mutex_init(Mutex *m) { InitializeCriticalSection(m); }
FORCE_INLINE void mutex_lock(Mutex *m) { EnterCriticalSection(m); }
FORCE_INLINE void mutex_unlock(Mutex *m) { LeaveCriticalSection(m); }
FORCE_INLINE void mutex_destroy(Mutex *m) { DeleteCriticalSection(m); }
#else
typedef pthread_mutex_t Mutex;
FORCE_INLINE void mutex_init(Mutex *m) { pthread_mutex_init(m, NULL); }
FORCE_INLINE void mutex_lock(Mutex *m) { pthread_mutex_lock(m); }
FORCE_INLINE void mutex_unlock(Mutex *m) { pthread_mutex_unlock(m); }
FORCE_INLINE void mutex_destroy(Mutex *m) { pthread_mutex_destroy(m); }
#endif

// -------- Атомарные операции --------
#ifdef _WIN32
FORCE_INLINE long atomic_increment(volatile long *a) {
    return InterlockedIncrement(a);
}
FORCE_INLINE long atomic_decrement(volatile long *a) {
    return InterlockedDecrement(a);
}
FORCE_INLINE long atomic_add(volatile long *a, long val) {
    return InterlockedExchangeAdd(a, val);
}
FORCE_INLINE long atomic_cas(volatile long *a, long expected, long desired) {
    return InterlockedCompareExchange(a, desired, expected);
}
#else
FORCE_INLINE int atomic_increment(volatile int *a) {
    return atomic_fetch_add(a, 1) + 1;
}
FORCE_INLINE int atomic_decrement(volatile int *a) {
    return atomic_fetch_sub(a, 1) - 1;
}
FORCE_INLINE int atomic_add(volatile int *a, int val) {
    return atomic_fetch_add(a, val);
}
FORCE_INLINE int atomic_cas(volatile int *a, int expected, int desired) {
    atomic_compare_exchange_strong(a, &expected, desired);
    return expected;
}
#endif

typedef struct {
    void *(*alloc)(void *ctx, size_t size);
    void (*free)(void *ctx, void *ptr);
    void (*reset)(void *ctx);
} Allocator;

typedef struct {
    Allocator *api; // Твоя структура с функциями
    void *data;     // Сама арена или NULL для malloc
} Context;

static void *std_alloc_impl(void *ctx, size_t size) {
    (void)ctx; // Контекст не нужен для malloc
    return malloc(size);
}

static void std_free_impl(void *ctx, void *ptr) {
    (void)ctx;
    free(ptr);
}

static void std_reset_impl(void *ctx) {
    (void)ctx;
}

static const Allocator STD_ALLOCATOR_API = {
    .alloc = std_alloc_impl, .free = std_free_impl, .reset = std_reset_impl};

const Context STD_CONTEXT = {.api = (Allocator *)&STD_ALLOCATOR_API,
                             .data = NULL};

#define BALLOC(a, ctx, sz) ((a).alloc((ctx), (sz)))
#define BFREE(a, ctx, ptr) ((a).free((ctx), (ptr)))
#define BRESET(a, ctx) ((a).reset((ctx)))

#endif // BASE_H