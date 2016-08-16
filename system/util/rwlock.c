#include <stdio.h>
#include <pthread.h>

#include <dlog.h>
#include <rwlock.h>

int lock_rw(urw_lock *l, int type)
{
    if (type == 2) {
        __sync_fetch_and_add(&l->num_writers, 1);
        while (l->num_readers);
        if (pthread_spin_lock(&l->real_lock) != 0) {
            perror("pthread_spin_lock");
            exit(EXIT_FAILURE);
        }
        return 0;
    }
    else if (type == 1) {
        while (l->num_writers);
        __sync_fetch_and_add(&l->num_readers, 1);
        return 0;
    }
    else {
        UEHANDLE("lock_rw: type must be 1 or 2");
    }
    /* not reached */
    return -1;
}

int unlock_rw(urw_lock *l, int type)
{
    if (type == 2) {
        __sync_fetch_and_add(&l->num_writers, -1);
        if (pthread_spin_unlock(&l->real_lock) != 0) {
            perror("pthread_spin_unlock");
            exit(EXIT_FAILURE);
        }
    }
    else if (type == 1) {
        __sync_fetch_and_add(&l->num_readers, -1);
    }
    return 0;
}

void init_urwlock(urw_lock *l)
{
	l->num_readers = 0;
	l->num_writers = 0;
	if (pthread_spin_init(&l->real_lock, PTHREAD_PROCESS_SHARED) != 0) {
		perror("pthread_spin_init");
		exit(EXIT_FAILURE);
	}
}
