#include "lib/manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

double LEFT  = 1;
double RIGHT = 2000000;
double PRECISION = 0.0000001;

struct task {
    size_t size_of_structure;
    double left;
    double step;
    uint64_t num_steps;
};

// Вычисление одного шага по формуле остаточного члена метода прямоугольников
// f(x) = sin(x)
static double get_step() {
    double max_ddf = sin(RIGHT);
    if (max_ddf == 0) {
        return 1;
    }
    return sqrt(24 * PRECISION / fabs(max_ddf));
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <address> <port> <max_time> <num_nodes>\n", argv[0]);
        return 1;
    }
    char *addr = argv[1];
    char *port = argv[2];
    time_t max_time = atol(argv[3]);
    if (!max_time) {
        fprintf(stderr, "Wrong time\n");
        return 1;
    }
    long num_nodes = atol(argv[4]);
    if (!num_nodes) {
        fprintf(stderr, "Number of nodes should be positive!\n");
        return 1;
    }

    INFO_MANAGER info_manager;

    if (info_manager_init(&info_manager, addr, port, max_time, num_nodes)) {
        fprintf(stderr, "Unable to init manager\n");
        return 1;
    }
    double step = get_step();
    uint64_t num_steps = (uint64_t)(ceil(fabs(RIGHT - LEFT) / step)) + 2;
    step = (RIGHT - LEFT) / num_steps;

    struct task *tasks = calloc(num_nodes, sizeof(*tasks));
    if (!tasks) return 1;
    
    double left = LEFT;
    for (unsigned i = 0; i < num_nodes; ++i) {
        tasks[i].left = left;
        tasks[i].step = step;
        tasks[i].num_steps = num_steps / num_nodes;
        if (i < num_steps % num_nodes)
            ++tasks[i].num_steps;
        tasks[i].size_of_structure = sizeof(*tasks);
        left += step * tasks[i].num_steps;
    }

    double *ans = calloc(num_nodes, sizeof(*ans));
    if (start_manager(&info_manager, sizeof(*tasks), num_nodes, (char *)tasks, (char *)ans) < 0) {
        free(tasks);
        free(ans);
        printf("Error in start manager!\n");
        return 1;
    }
    double res = 0;
    for (int i = 0; i < num_nodes; ++i) {
        res += ans[i];
    }
    free(tasks);
    free(ans);
    printf("Result: %lf\n", res);
}
