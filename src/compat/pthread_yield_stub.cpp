#include <errno.h>
#include <cstddef>

extern "C" int sysctl(int *name, int nlen, void *oldval, size_t *oldlenp, void *newval, size_t newlen)
{
    errno = ENOSYS;
    return -1;
}

extern "C" int sched_yield(void);

extern "C" int pthread_yield(void)
{
    return sched_yield();
}
