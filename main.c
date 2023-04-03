#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>

uint64_t prev_micro = 0;
uint64_t max = 0;
uint64_t diff = 0;
uint8_t first_time = 1;
uint64_t i = 0;
uint64_t diff_arr[65535];

struct period_info {
    struct timespec next_period;
    long period_ns;
};

static void inc_period(struct period_info *pinfo)
{
    pinfo->next_period.tv_nsec += pinfo->period_ns;

    while(pinfo->next_period.tv_nsec >= 1000000000) {
        pinfo->next_period.tv_sec++;
        pinfo->next_period.tv_nsec -= 1000000000;
    }
}

static void periodic_task_init(struct period_info *pinfo)
{
    pinfo->period_ns = 1000000;

    clock_gettime(CLOCK_MONOTONIC, &(pinfo->next_period));
}

static void do_rt_task()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    uint64_t microseconds = (ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
    diff = microseconds - prev_micro;
    diff_arr[i] = diff;
    if(max < diff && !first_time) {
        max = diff;
    } else {
        first_time = 0;
    }
    prev_micro = microseconds;
}

static void wait_rest_of_period(struct period_info *pinfo)
{
    inc_period(pinfo);

    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &pinfo->next_period, NULL);
}

void *cyclic_task(void *data)
{
    struct period_info pinfo;

    periodic_task_init(&pinfo);

    for(;;) {
        do_rt_task();
        i++;
        if(i > 65535) {
            int ret1 = 1;
            pthread_exit(&ret1);
        }
        wait_rest_of_period(&pinfo);
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;
    int ret;

    if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
        printf("mlockall failed: %m\n");
        exit(-2);
    }

    ret = pthread_attr_init(&attr);
    if(ret) {
        printf("init pthread attributes failed\n");
        goto out;
    }

    ret = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
    if(ret) {
        printf("pthread setstacksize failed\n");
        goto out;
    }

    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if(ret) {
        printf("pthread setschedpolicy failed\n");
        goto out;
    }

    param.sched_priority = 80;
    ret = pthread_attr_setschedparam(&attr, &param);
    if(ret) {
        printf("pthread setschedparam failed\n");
        goto out;
    }

    ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    if(ret) {
        printf("pthread setinheritsched\n");
        goto out;
    }

    ret = pthread_create(&thread, &attr, cyclic_task, NULL);
    if(ret) {
        printf("pthread create failed %d\n", ret);
        goto out;
    }

    ret = pthread_join(thread, NULL);
    if(ret) {
        printf("join pthread failed %d\n", ret);
    }

    FILE *out_file = fopen("output.txt", "w");
    if(out_file == NULL) {
        printf("Could not open file!\n");
        exit(-1);
    }
    printf("Here\n");
    for(int j = 0; j < 65535; j++) {
        fprintf(out_file, "Diff: %lu\n", diff_arr[j]);
    }
    fprintf(out_file, "Max: %lu\n", max);

    fclose(out_file);

out:
    return ret;
}
