#include <pthread.h>

typedef struct pid_info_t {
    pid_t pid;
    int fds_backed_up;
    int first_parent_signaled;
    int vforked;
    pthread_spinlock_t lock;
} pid_info;


/* process-wide vars */
extern pid_info *pid_meta;
extern int pid_meta_ind;
extern int pid_meta_ind_set;
extern int this_vforked;
extern int this_signaled_by_parent;

/* functions */
int open_map_pid_meta_mem(void);
int init_vfork_signaling(void);
int restore_fd_backup(void);
void close_cloexec(void);
int maybe_signal_parent(void);
int get_pid_meta_ind(void);
int get_new_pid_meta_ind(void);
int remove_pid_slot(void);
void wake1(int a);
void wake2(int a);
void exit_intercept(void);
int trylock_pid_info(pid_info *pi);
int unlock_pid_info(pid_info *pi);
