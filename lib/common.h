typedef enum
{
    EXP,
    SIN,
    SQR,
    NOT_SUPPORT,
} FUNC_TABLE;

struct worker_result {
    double value;
};

struct node_info {
    time_t max_worker_time;
    int n_cores;
};
