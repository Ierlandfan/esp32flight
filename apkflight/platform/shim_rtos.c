#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

/* ---- tasks over detached pthreads ---- */

typedef struct {
    TaskFunction_t fn;
    void *arg;
} task_wrap_t;

static void *task_tramp(void *p)
{
    task_wrap_t w = *(task_wrap_t *)p;
    free(p);
    w.fn(w.arg);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, int stack,
                       void *arg, int prio, TaskHandle_t *out)
{
    (void)name; (void)stack; (void)prio;
    task_wrap_t *w = malloc(sizeof(*w));
    w->fn = fn;
    w->arg = arg;
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&t, &attr, task_tramp, w);
    pthread_attr_destroy(&attr);
    if (out != NULL) {
        *out = (TaskHandle_t)t;
    }
    return rc == 0 ? pdPASS : pdFALSE;
}

void vTaskDelay(TickType_t ticks)
{
    usleep((useconds_t)ticks * 1000);
}

void vTaskDelete(TaskHandle_t task)
{
    if (task == NULL) {
        pthread_exit(NULL);   /* FreeRTOS idiom: delete the calling task */
    }
    /* deleting other tasks is not used by the core */
}

/* ---- mutexes (recursive, matches FreeRTOS mutex semantics closely
 *      enough for this codebase) ---- */

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *m = malloc(sizeof(*m));
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    return xSemaphoreCreateMutex();
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    (void)ticks;   /* callers use it as blocking; timeouts unused on desktop */
    return pthread_mutex_lock((pthread_mutex_t *)s) == 0 ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    return pthread_mutex_unlock((pthread_mutex_t *)s) == 0 ? pdTRUE : pdFALSE;
}
