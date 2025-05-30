#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <assert.h>

#include "common.h"
#include "worker.h"

//==================
// Управление сетью
//==================
static void worker_close_socket(INFO_WORKER* worker)
{
    if (close(worker->server_conn_fd) == -1)
    {
        fprintf(stderr, "[worker_close_socket] Unable to close() worker socket\n");
        exit(EXIT_FAILURE);
    }
    worker->server_conn_fd = -1;
}

static bool worker_connect_to_server(INFO_WORKER* worker)
{
    if (worker->server_conn_fd == -1) {
        worker->server_conn_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (worker->server_conn_fd == -1)
        {
            fprintf(stderr, "[worker_connect_to_server] Unable to create socket()\n");
            exit(EXIT_FAILURE);
        }
    }
    if (connect(worker->server_conn_fd, &worker->server_addr, sizeof(worker->server_addr)) == -1)
    {
        if (errno == ECONNREFUSED)
        {
            worker_close_socket(worker);

            return false;
        }

        fprintf(stderr, "[connect_to_master] Unable to connect(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return true;
}

//=================================
// Передача данных по сети.
//=================================

static bool get_data(INFO_WORKER* worker)
{
    size_t bytes_read = recv(worker->server_conn_fd, worker->data, worker->size_of_structure, MSG_WAITALL);
    if (bytes_read != worker->size_of_structure)
    {
        fprintf(stderr, "[get_data] unable to recv data from server\n");
        return false;
    }

    return true;
}

static bool send_node_info(INFO_WORKER *worker)
{
    if (!worker)
        return false;

    size_t bytes_written = write(worker->server_conn_fd, &worker->n_cores,
            sizeof(worker->n_cores));
    if (bytes_written != sizeof(worker->n_cores))
    {
        fprintf(stderr, "Unable to send node info to server\n");
        return false;
    }

    return true;
}

//============================
// Интерфейс исполнителя
//============================

int distributed_counting(INFO_WORKER *worker, void*(thread_func(void*)))
{
    time_t start_time = time(NULL);
    // Проверка валидности запрашиваемого числа ядер
    if (worker->n_cores > get_nprocs()) {
        fprintf(stderr, 
                "[distributed_counting] the number of processors currently available in the system is less than required\n");
        return -1;
    }

    int threads_num = worker->n_cores;
    
    if (!threads_num) {
        fprintf(stderr, "Number of required cores should be greater than zero\n");
        exit(EXIT_FAILURE);
    } else if (threads_num == 1) {
        thread_func(worker->data);
        fprintf(stderr, "TIME: %d\nn_cores=%d\n", time(NULL) - start_time, worker->n_cores);
        return 0;
    }
    pthread_t threads[threads_num];
    char *args[threads_num];

    for (int i = 0; i < threads_num; ++i) {
        args[i] = worker->data + worker->size_of_structure * i;
         
        if (pthread_create(&threads[i], NULL, thread_func, args[i])) {
            fprintf(stderr, "Unable to create thread\n");
            return -1;
        }
    }

    // Ждём завершения потоков
    for (int i = 0; i < threads_num; ++i)
    {
        if (pthread_join(threads[i], NULL)) {
            fprintf(stderr, "Unable to join a thread\n");
            return -1;
        }
    }
    fprintf(stderr, "TIME: %d\nn_cores=%d\n", time(NULL) - start_time, worker->n_cores);

    return 0;
}

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void worker_add_result(INFO_WORKER *worker, char *result, void(add_func(char*, char*)))
{
    pthread_mutex_lock(&mutex);
    add_func(worker->result, result);
    pthread_mutex_unlock(&mutex);
}

int init_worker(INFO_WORKER *worker, size_t size_of_structure, size_t size_of_result, 
        int n_cores, time_t max_time, char *node, char *service)
{
    worker->n_cores = n_cores;
    worker->max_time = max_time;
    worker->size_of_structure = size_of_structure;
    worker->size_of_result = size_of_result;
    worker->result = calloc(size_of_result, 1);
    if (!worker->result) {
        fprintf(stderr, "[init_worker] Unable to allocate memory\n");
        return -1;
    }
    
    worker->data = calloc(size_of_structure, 1);
    if (!worker->data) {
        fprintf(stderr, "[init_worker] Unable to allocate memory\n");
        return -1;
    }

    worker->server_conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (worker->server_conn_fd == -1)
    {
        fprintf(stderr, "[init_worker] Unable to create socket()\n");
        return -1;
    }

    // Формируем желаемый адрес для подключения.
    struct addrinfo hints;

    struct addrinfo* res;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = 0;
    hints.ai_protocol = 0;

    if (getaddrinfo(node, service, &hints, &res) != 0)
    {
        fprintf(stderr, "[init_worker] Unable to call getaddrinfo()\n");
        return -1;
    }

    // Проверяем, что для данного запроса существует только один адрес.
    if (res->ai_next != NULL)
    {
        fprintf(stderr, "[init_worker] Ambigous result of getaddrinfo()\n");
        return -1;
    }

    worker->server_addr = *res->ai_addr;
    freeaddrinfo(res);
    
    return 0;
}

int connect_to_server(INFO_WORKER *worker) {
    // Подключение к серверу.
    bool connected_to_server = worker_connect_to_server(worker);
    while (!connected_to_server)
    {
        // Ожидаем, пока сервер проснётся.
        sleep(1U);

        printf("Wait for server to start\n");

        connected_to_server = worker_connect_to_server(worker);
    }

    // Отправка данных об узле.
    bool success = send_node_info(worker);
    if (!success)
    {
        worker_close_socket(worker);
        return -1;
    }

    // Получение данных.
    success = get_data(worker);
    if (!success)
    {
        worker_close_socket(worker);
        return -1;
    }

    return 0;
}

int send_result(INFO_WORKER *worker)
{
    if (!worker)
        return -1;

    size_t bytes_written = write(worker->server_conn_fd, &worker->size_of_result, sizeof(worker->size_of_result));
    if (bytes_written != sizeof(worker->size_of_result))
    {
        fprintf(stderr, "Unable to send result to server\n");
        return -1;
    }

    bytes_written = write(worker->server_conn_fd, worker->result, worker->size_of_result);
    if (bytes_written != worker->size_of_result)
    {
        fprintf(stderr, "Unable to send result to server\n");
        return -1;
    }
    return 0;
}

void worker_close(INFO_WORKER *worker)
{
    printf("[worker_close]\n");
    // Освобождение сокета.
    if (worker->server_conn_fd >= 0)
        worker_close_socket(worker);

    if (worker->result) {
        free(worker->result);
    }
    worker->result = NULL;
}

//============================
// Тестирование исполнителя.
//============================
struct thread_args
{
    int func_id;
    long long parts;
    double left;
    double step;
    double retval;
};

#if defined(TEST)
    static void *test_thread_func(void *t_args)
    {
        double result = 0;
        struct thread_args *args = (struct thread_args *) t_args;
#ifdef DEBUG
        printf("step=%lf, func_id=%d, left=%lf, parts=%lld\n", args->step, args->func_id, args->left, args->parts);
#endif
        for (long long i = 0; i < args->parts; ++i) {
            result += args->step * func_val(args->func_id, args->left + args->step * i + args->step / 2);
#ifdef DEBUG
            printf("func(%lf) = %lf\n", args->left + args->step * i + args->step / 2, 
                                    func_val(EXP, args->left + args->step * i + args->step / 2));
#endif
        }
        args->retval = result;
#ifdef DEBUG
            printf("retval = %lf\n", result);
#endif
        return NULL;
    }

    void test(void)
    {
        printf("Testing:\n");
        assert(func_val(EXP, 10) == exp(10));
        assert(func_val(SIN, 10) == sin(10));
        assert(func_val(SQR, 10) == 10 * 10);

        for (int n_cores = 1; n_cores <= 8; ++n_cores) {
            printf("%d CORES:\n", n_cores);
            time_t max_time = 10;
            int num_steps = 4;
            double step = 1.;
            double left = 0.;
            struct worker_data data = {EXP, .left = left, .step = step, .num_steps = num_steps};
            double expected = 0;
            for(int i = 0; i < num_steps; ++i) {
                expected += step * func_val(EXP, left + i * step + step / 2);
                printf("expected_func(%lf) = %lf\n", left + i * step + step / 2,
                        func_val(EXP, left + i * step + step / 2));
            }

            INFO_WORKER worker = {.max_time = max_time, .n_cores = n_cores, .data = data};
            double res = distributed_counting(&worker, test_thread_func);
            printf("expected = %lf\n", expected);
            printf("result = %lf\n", res);
            assert(expected == res);
        }
    }
#endif
