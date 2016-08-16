/*
 * [[[ XXX: There is a huge difference
 * between read/write locks and spinlocks
 */
/** structures **/
typedef struct rw_lock_t {
    int num_readers;
    int num_writers;
    pthread_spinlock_t real_lock;
} urw_lock;
/*
 * :]]]
 */

/*
 * XXX: [[[: Chang this API to
 * down_read/write(lock) and up_read/write(lock) api
 */
/* read-write lock with write priority
 * type = 1 : reader
 * type = 2 : writer
 */
int lock_rw(urw_lock *l, int type);
int unlock_rw(urw_lock *l, int type);

#define down_write(l) 	(lock_rw((l), 2))
#define up_write(l) 	(unlock_rw((l), 2))
#define down_read(l) 	(lock_rw((l), 1))
#define up_read(l) 		(unlock_rw((l), 1))
/*
 * ]]]
 */

/*
 * Init api
 */
void init_urwlock(urw_lock *l);
