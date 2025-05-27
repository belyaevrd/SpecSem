#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <endian.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
#include <memory.h>
#include <poll.h>
#include <sched.h>
#include <pthread.h>
#include <netdb.h>
#include "manager.h"

//! Состояния рабочего узла
typedef enum
{
    CONNECTION_EMPTY,
    GET_INFO,   // -> WAIT_TASK
    WAIT_TASK, //-> WAIT_ANS, WORK_FINISHED 
    WAIT_ANS, // -> WAIT_TASK
    WORK_FINISHED
} WORKER_STATE;

//! Дескриптор рабочего узла
typedef struct
{
    // Дескриптор сокета для обмена данными с рабочим узлом.
    int worker_sock_fd;
    // Количество ядер на рабочем узле.
    int n_cores;
    // Текущее состояние рабочего узла.
    WORKER_STATE state;
} WORKER_CONN;

static void poll_server_wait_for_worker(struct pollfd* pollfds, INFO_MANAGER* server)
{
    struct pollfd* pollfd = &pollfds[0U];

    pollfd->fd      = server->listen_sock_fd;
    pollfd->events  = POLLIN;
    pollfd->revents = 0U;
}

static void poll_server_do_not_wait_for_workers(struct pollfd* pollfds)
{
    struct pollfd* pollfd = &pollfds[0U];

    pollfd->fd      = -1;
    pollfd->events  = 0U;
    pollfd->revents = 0U;
}

static void poll_manager_wait_for_answer(struct pollfd* pollfds, size_t conn_i, WORKER_CONN *work) {
    struct pollfd* pollfd = &pollfds[1 + conn_i];

    pollfd->fd      = work->worker_sock_fd;
    pollfd->events  = POLLIN | POLLHUP;
    pollfd->revents = 0U;
}

static void poll_manager_do_not_wait_for_ans(struct pollfd* pollfds, size_t conn_i){
    struct pollfd* pollfd = &pollfds[conn_i + 1];

    pollfd->fd      = -1;
    pollfd->events  = 0U;
    pollfd->revents = 0U;
}

static void poll_manager_wait_work_info(struct pollfd* pollfds, size_t conn_i, WORKER_CONN *work) {
    struct pollfd* pollfd = &pollfds[1 + conn_i];

    pollfd->fd      = work->worker_sock_fd;
    pollfd->events  = POLLIN|POLLHUP;
    pollfd->revents = 0U;
}

//============================
// Процедуры сервера
//============================
static bool manager_init_socket(INFO_MANAGER* manager)
{
    if (manager->is_init == false) {
        fprintf(stderr, "[manager_init] Not init Info Manager!\n");
        return false;
    }
    // Создаём сокет, слушающий подключения клиентов.
    manager->listen_sock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
    if (manager->listen_sock_fd == -1)
    {
        fprintf(stderr, "[manager_init] Unable to create socket!\n");
        return false;
    }

    // Запрещаем перевод слушающего сокета в состояние TIME_WAIT.
    int setsockopt_yes = 1;
    if (setsockopt(manager->listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to set SO_REUSEADDR socket option\n");
        return false;
    }

    if (bind(manager->listen_sock_fd, (struct sockaddr*) &(manager->listen_addr), sizeof(manager->listen_addr)) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to bind\n");
        return false;
    }

    // Активируем очередь запросов на подключение.
    if (listen(manager->listen_sock_fd, manager->num_nodes /* Размер очереди запросов на подключение */) == -1)
    {
        fprintf(stderr, "[manager_init] Unable to listen() on a socket\n");
        return false;
    }
    return true;
}

static bool manager_close_listen_socket(INFO_MANAGER* manager) {

    if (close(manager->listen_sock_fd) == -1)
    {
        fprintf(stderr, "[manager_close_listen_socket] Unable to close() listen-socket\n");
        return false;
    }
    return true;
}

static bool manager_accept_connection_request(INFO_MANAGER* manager, WORKER_CONN* conn)
{
    DEBUG("Wait for worker_node to connect\n");

    // Создаём сокет для клиента из очереди на подключение.
    conn->worker_sock_fd = accept(manager->listen_sock_fd, NULL, NULL);
    if (conn->worker_sock_fd == -1)
    {
        fprintf(stderr, "[manager_accept_connection_request] Unable to accept() connection on a socket\n");
        return false;
    }

    // Disable Nagle's algorithm:
    int setsockopt_arg = 1;
    if (setsockopt(conn->worker_sock_fd, IPPROTO_TCP, TCP_NODELAY, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
    {
        fprintf(stderr, "[manager_accept_connection_request] Unable to enable TCP_NODELAY socket option");
        return false;
    }

    // Disable corking:
    setsockopt_arg = 0;
    if (setsockopt(conn->worker_sock_fd, IPPROTO_TCP, TCP_CORK, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
    {
        fprintf(stderr, "[manager_accept_connection_request] Unable to disable TCP_CORK socket option");
        return false;
    }

    DEBUG("Worker connected\n");
    conn->state = GET_INFO;
    return true;
}

static bool manager_get_worker_info(WORKER_CONN *work)
{
    size_t bytes_read = recv(work->worker_sock_fd, &(work->n_cores), sizeof(work->n_cores), MSG_WAITALL);
    if (bytes_read != sizeof(work->n_cores))
    {
        fprintf(stderr, "Unable to recv n_cores info from worker: bytes_read=%d\n", bytes_read);
        return false;
    }
    work->state = WAIT_TASK;
    DEBUG("Connect worker with cores : %lu\n",work->n_cores);
    return true;
}

static int manager_send_tasks(WORKER_CONN *work, size_t size_of_structure, char *data) 
{

    size_t bytes_written = write(work->worker_sock_fd, data, size_of_structure);
    if (bytes_written != size_of_structure)
    {
        fprintf(stderr, "Unable to send a task to worker\n");
        return -1;
    }
    DEBUG("Sent data with size: %lu\n", size_data);
    work->state = WAIT_ANS;
    return 0;
}

static int manager_get_worker_ans(WORKER_CONN *work, char **ans) {
    size_t ans_size = 0;
    size_t bytes_read = recv(work->worker_sock_fd, &ans_size, sizeof(ans_size), MSG_WAITALL);
    if (bytes_read != sizeof(ans_size))
    {
        fprintf(stderr, "can't get size: get %lu bytes from worker, expected %ld\n",bytes_read, sizeof(ans_size));
        return 0;
    }
    bytes_read = recv(work->worker_sock_fd, *ans, ans_size, MSG_WAITALL);
    if (bytes_read != ans_size)
    {
        fprintf(stderr, "Get %lu bytes from worker, expected %lu\n",bytes_read, ans_size);
        return 0;
    }
    printf("[manager_get_worker_ans] get %lf\n", **(double **)ans);
    return bytes_read;
}

static bool manager_close_worker_socket(WORKER_CONN *work) {
    size_t end_tasks = 0;
    write(work->worker_sock_fd,&end_tasks,sizeof(end_tasks));
    if (close(work->worker_sock_fd) == -1)
    {
        fprintf(stderr, "[manager_close_worker_socket] Unable to close() worker-socket\n");
        return false;
    }
    work->worker_sock_fd = -1;
    return true;
}

static bool wait_and_get_info_workers(INFO_MANAGER* manager, WORKER_CONN *works, struct pollfd *pollfds) {
    size_t num_connected_workers = 0U;
    size_t num_init_workers = 0U;
    while (num_init_workers != manager->num_nodes)
    {
        if (num_connected_workers != manager->num_nodes) {
            poll_server_wait_for_worker(pollfds, manager);
        } else {
            poll_server_do_not_wait_for_workers(pollfds);
        }
        int pollret = poll(pollfds, 1U + num_connected_workers, -1);
        if (pollret == -1)
        {
            fprintf(stderr, "Unable to poll-wait for data on descriptors!\n");
            goto error;
        }

        if (pollfds[0U].revents & POLLIN)
        {
            if(!manager_accept_connection_request(manager, &works[num_connected_workers])){
                goto error;
            }

            poll_manager_wait_work_info(pollfds, num_connected_workers, &works[num_connected_workers]);
            num_connected_workers++;
        } 

        for (size_t conn_i = 0U; conn_i < num_connected_workers; ++conn_i)
        {
            if (pollfds[1U + conn_i].revents & POLLHUP)
            {  
                fprintf(stderr, "Unexpected POLLHUP\n");
                goto error;
            }

            if (pollfds[1U + conn_i].revents & POLLIN)
            {
                switch (works[conn_i].state)
                {
                case CONNECTION_EMPTY:
                case WORK_FINISHED:
                case WAIT_ANS:
                    fprintf(stderr, "Unexpected state!\n");
                    goto error;
                case GET_INFO:
                    if(!manager_get_worker_info(&works[conn_i])) {
                        goto error;
                    }
                    num_init_workers++;
                    break;
                case WAIT_TASK:
                }
            }
        }
    }
    return true;
error:
    for(size_t i = 0; i < num_connected_workers; ++i) {
        manager_close_worker_socket(&works[i]);
    }
    return false;
}

//============================
// Интерфейс сервера
//============================
int start_manager(INFO_MANAGER *manager, size_t size_of_structure, size_t num_tasks,
        char *tasks, char *ans) 
{
    if (!manager || !tasks || !ans)
        return -1;
    if (!size_of_structure || !manager->max_time || !manager->is_init || !manager->num_nodes)
        return -1;

    WORKER_CONN* works = calloc(manager->num_nodes, sizeof(WORKER_CONN));
    struct pollfd* pollfds = calloc(manager->num_nodes + 1U, sizeof(struct pollfd));

    if (works == NULL || pollfds == NULL) {
        goto error_clear;
    }

    for (size_t conn_i = 0U; conn_i < manager->num_nodes; conn_i++)
    {
        works[conn_i].state = CONNECTION_EMPTY;
    }

    if (!manager_init_socket(manager)) {
        goto error_clear;
    }
    if(!wait_and_get_info_workers(manager, works, pollfds)) {
        manager_close_listen_socket(manager);
        goto error_clear;
    }
    DEBUG("All workers connected\n");

    if (!manager_close_listen_socket(manager)) {
        goto error_close;
    }

    time_t start_time = time(NULL);
    char *ptr_tasks = tasks;

    for (size_t conn_i = 0; conn_i < manager->num_nodes; ++conn_i) {
        if(manager_send_tasks(&works[conn_i], size_of_structure, ptr_tasks))
            goto error_close;
        poll_manager_wait_for_answer(pollfds,conn_i,&works[conn_i]);
        ptr_tasks += size_of_structure;
    }

    size_t get_answers = 0;
    while(get_answers != num_tasks) {
        time_t max_wait_time = manager->max_time - (time(NULL) - start_time);
        int pollret = poll(pollfds, 1U + manager->num_nodes, max_wait_time);
        if (pollret == -1)
        {
            fprintf(stderr, "Unable to poll-wait for data on descriptors!\n");
            goto error_close;
        }

        // fprintf(stderr, "[start_manager] pollret=%d\n", pollret);
        // if (pollret != num_tasks) {
        //     fprintf(stderr, "Some workers didn't send their answers\n");
        //     goto error_close;
        // }
        for (size_t conn_i = 0U; conn_i < manager->num_nodes; ++conn_i) {
            if (pollfds[1U + conn_i].revents & POLLHUP)
            {  
                fprintf(stderr, "Unexpected POLLHUP\n");
                goto error_close;
            }

            if (pollfds[1U + conn_i].revents & POLLIN)
            {
                int ans_size = 0;
                switch (works[conn_i].state) {
                case CONNECTION_EMPTY:
                case GET_INFO:
                    fprintf(stderr, "Unexpected state!\n");
                    goto error_close;
                case WAIT_ANS:
                    printf("[start_manager] getting answers\n");
                    if((ans_size = manager_get_worker_ans(&works[conn_i], &ans)) == 0) {
                        goto error_close;
                    }
                    ans += ans_size;
                    ++get_answers;
                    manager_close_worker_socket(&works[conn_i]);
                    works[conn_i].state = WORK_FINISHED;
                    poll_manager_do_not_wait_for_ans(pollfds,conn_i);
                case WAIT_TASK:
                case WORK_FINISHED:
                }
            }
        }
    }
    free(pollfds);
    free(works);
    return 0;
error_close:
    for(size_t i = 0; i < manager->num_nodes; ++i) {
        manager_close_worker_socket(&works[i]);
    }
    DEBUG("Fall in error_close!\n");
error_clear:
    free(pollfds);
    free(works);
    DEBUG("Fall in error_clear!\n");
    return -1;    
}

int info_manager_init(INFO_MANAGER *manager, const char *addr, const char *port, time_t time, int num_nodes) {
    manager->is_init = false;
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(addr, port, &hints, &res)) {
        return -1;
    }

    manager->listen_addr = *res->ai_addr;
    manager->max_time = time;
    manager->num_nodes = num_nodes;
    manager->is_init = true;
    freeaddrinfo(res);
    return 0;
}
