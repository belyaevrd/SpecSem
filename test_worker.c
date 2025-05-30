#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <math.h>

#include "lib/common.h"
#include "lib/worker.h"


// node = "127.0.0.1"
// service = "1337"
double LEFT  = 0;
double RIGHT = 10;
double PRECISION = 0.0001;

struct task {
    size_t size_of_structure;
    double left;
    double step;
    uint64_t parts;
};

// Данные исполнителя
INFO_WORKER worker = {};

//============================
// Процедура для запуска потоков.
//============================
void add_func(char *a, char *b)
{
    double *A = (double *)a;
    double *B = (double *)b;
    *A += *B;
}

void *func(void *t_args)
{
    time_t start_time = time(NULL);
    printf("Begin counting\n");
    double result = 0;
    struct task *args = (struct task *) t_args;
    printf("step=%lf, left=%lf, parts=%lld\n", args->step, args->left, args->parts);
    double left = args->left;
    double step = args->step;
    double parts = args->parts;
    double x = left + step / 2;
    for (long long i = 0; i < parts; ++i) {
        result += step * sin(x);
        x += step;
    }

    worker_add_result(&worker, (char *)&result, add_func);
    fprintf(stderr, "TIME #: %ds\n", time(NULL) - start_time);
    return NULL;
}

//============================
// Процедура для разбиения данных для потоков.
//============================
int data_for_threads(INFO_WORKER *worker)
{
    struct task *task = (struct task *)worker->data;
    if (!task) return -1;

    struct task *tasks = calloc(worker->n_cores, sizeof(*tasks));
    if (!tasks) return -1;

    double left = task->left;
    for (int i = 0; i < worker->n_cores; ++i) {
        tasks[i].left = left;
        tasks[i].step = task->step;
        tasks[i].parts = task->parts / worker->n_cores;
        if (i < task->parts % worker->n_cores)
            ++tasks[i].parts;
        left += tasks[i].step * tasks[i].parts;
    }
    
    worker->data = (char *)tasks;
//    worker->size_of_structure = sizeof(*tasks);
    free(task);
    return 0;
}

//============================
// Основная процедура исполнителя.
//============================

int main( int argc, char** argv) 
{
#if defined(TEST)
    test();
#else
    time_t max_time = 10;
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <address> <port> <num_cores>\n", argv[0]);
        return 1;
    }
    int n_cores = atol(argv[3]);
    if (!n_cores) {
        fprintf(stderr, "Number of nodes should be positive!\n");
        return 1;
    }

    if (init_worker(&worker, sizeof(struct task), sizeof(double), n_cores, max_time, argv[1], argv[2])) {
        fprintf(stderr, "[init_worker] error\n");
        return EXIT_FAILURE;
    }

    if (connect_to_server(&worker)) {
        fprintf(stderr, "[connect_to_server] error\n");
        return EXIT_FAILURE;
    }

    if (data_for_threads(&worker)) {
        fprintf(stderr, "[data_for_threads] error\n");
        return EXIT_FAILURE;
    }

    // Вычисление результата
    if (distributed_counting(&worker, func)) {
        fprintf(stderr, "[distributed_counting] error\n");
        worker_close(&worker);
        return EXIT_FAILURE;
    }

    // Отправка результата
    if (send_result(&worker)) {
        fprintf(stderr, "[send_result] error\n");
        worker_close(&worker);
        return EXIT_FAILURE;
    }
    printf("[WORKER] Sent answer %lf\n", *(double *)worker.result);
    worker_close(&worker);

#endif // TEST

    printf("[WORKER] exit\n");
    return EXIT_SUCCESS;
}
