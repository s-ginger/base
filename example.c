#include "base.h"
#include <stdio.h>

#define THREAD_COUNT 4
#define INCREMENTS 100000

Mutex print_mutex;
Barrier barrier;
volatile long counter = 0;
Once init_once = ONCE_INIT;

// Функция одноразовой инициализации
void init_counter(void) {
    counter = 0;
    mutex_lock(&print_mutex);
    printf("[Once] Counter initialized to zero\n");
    mutex_unlock(&print_mutex);
}

// Функция потока
void* worker(void* arg) {
    int id = (int)(intptr_t)arg;

    // Одноразовая инициализация
    once_execute(&init_once, init_counter);

    for (int i = 0; i < INCREMENTS; i++) {
        atomic_increment(&counter); // Атомарное увеличение
    }

    mutex_lock(&print_mutex);
    printf("Thread %d done incrementing\n", id);
    mutex_unlock(&print_mutex);

    barrier_wait(&barrier); // Ждем все потоки

    // Один поток выводит результат после барьера
    if (id == 0) {
        mutex_lock(&print_mutex);
        printf("[Barrier] All threads done. Counter = %ld (expected %d)\n", 
               counter, THREAD_COUNT * INCREMENTS);
        mutex_unlock(&print_mutex);
    }

    return NULL;
}

int main() {
    Thread threads[THREAD_COUNT];

    // Инициализация мьютекса и барьера
    mutex_init(&print_mutex);
    barrier_init(&barrier, THREAD_COUNT);

    // Создание потоков
    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_create(&threads[i], worker, (void*)(intptr_t)i);
    }

    // Ожидание завершения потоков
    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_join(threads[i]);
    }

    // Очистка
    barrier_destroy(&barrier);
    mutex_destroy(&print_mutex);

    return 0;
}