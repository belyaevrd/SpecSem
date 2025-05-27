//================
// Данные исполнителя.
//================
#include <sys/socket.h>

typedef struct
{
    // Дескриптор сокета для подключения к серверу.
    int server_conn_fd;

    // Адрес для подключению к серверу.
    struct sockaddr server_addr;

    // Максимальное время вычисления.
    time_t max_time;

    // Количество ядер.
    int n_cores;

    // Данные для вычисления интеграла.
    char *data;

    // Размер структуры данных
    size_t size_of_structure;
    
    // Размер структуры данных
    size_t size_of_result;

    // Результат вычислений.
    char *result;
} INFO_WORKER;


//================
// Интерфейс исполнителя.
//================

// Инициализация структуры исполнителя
int init_worker(INFO_WORKER *worker, size_t size_of_structure, size_t size_of_result, 
        int n_cores, time_t max_time, char *node, char *service);

// Подключение к серверу
int connect_to_server(INFO_WORKER *worker);

// Распределение вычисления по ядрам
int distributed_counting(INFO_WORKER *worker, void*(thread_func(void*)));

// Добавление результата одного потока
void worker_add_result(INFO_WORKER *worker, char *result, void(add_func(char*, char*)));

// Отправка результата серверу
int send_result(INFO_WORKER *worker);

// Закрытие открытого сокета
void worker_close(INFO_WORKER *worker);

#if defined(TEST)
void test(void);
#endif
