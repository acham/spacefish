#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

/* shmFS */
#include <utility.h>
#include <glibccall.h>
#include <dlog.h>
#include <meta_shm.h>
#include <data_shm.h>

#ifdef USE_METROHASH
#include <stdint.h>
#endif

extern int alloc_method_ind;

meta_bucket *upload_file(const char *name)
{
    dlog(1, "[ufs: upload_file] %s\n", name);
    int fd, rounds, off, i;
    unsigned int dbindex;
    meta_bucket *mb;
    data_bucket *db;
    void *buf;
    struct stat s;
    ssize_t c;

    if (real_stat(name, &s)) {
        return NULL;
    }
    if ((fd = real_open(name, O_RDONLY)) < 0) {
        return NULL;
    }
    if (!(mb = strict_create_mbucket(name))) {
        errno = EEXIST;
        return NULL;
    }
    buf = malloc(s.st_size);
    c = real_read(fd, buf, s.st_size);
    if (c != s.st_size) {
        return NULL;
    }
    real_close(fd);
    strcpy(mb->path, name);
    memcpy(&mb->s, &s, sizeof(struct stat));
    rounds = s.st_size / DBUCKET_BUF_SIZE;
    off = s.st_size % DBUCKET_BUF_SIZE;
    if (rounds == 0) {
        dbindex = get_free_dbucket_ix();
        db = get_dbucket_ptr(dbindex);
        db->bytes = 0;
        mb->first_dbucket = dbindex;
    }
    for (i = 0; i < rounds; i ++) {
        dbindex = get_free_dbucket_ix();
        if (i == 0) {
            mb->first_dbucket = dbindex;
        }
        else {
            db->next_ix = dbindex;
        }
        db = get_dbucket_ptr(dbindex);
        memcpy(db->buf, buf + i * DBUCKET_BUF_SIZE, DBUCKET_BUF_SIZE);
        db->bytes = DBUCKET_BUF_SIZE;
    }
    if (off > 0) {
        if (db->bytes == 0) {
            memcpy(db->buf, buf, off);
            db->bytes = off;
        }
        else {
            dbindex = get_free_dbucket_ix();
            db->next_ix = dbindex;
            db = get_dbucket_ptr(dbindex);
            memcpy(db->buf, buf + rounds * DBUCKET_BUF_SIZE, off);
            db->bytes = off;
        }
    }
    free(buf);
    return mb;
}

void upload_dir(const char *name)
{
    DIR *dir;
    struct dirent *entry;
    const char *fpath;

    if (!(dir = opendir(name)))
        return;
    if (!(entry = readdir(dir)))
        return;
    do {
        char path[PATH_MAX];
        int len = snprintf(path, PATH_MAX - 1, "%s/%s", name, entry->d_name);
        path[len] = 0;
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0)
                continue;
            upload_dir(path);
        }
        else {
            BUILD_ABS_PATH_1(fpath, path);
            upload_file(fpath);
            FREE_ABS_PATH_1(fpath, path);
        }
    } while ((entry = readdir(dir)));
    closedir(dir);
}

meta_bucket *traverse_next_bucket(meta_bucket *mb)
{
    int pos;
    if (!mb) {
        pos = 0;
    }
    else {
        pos = MB_IX(mb) + 1;
    }
    while (pos < META_BUCKETS) {
        if (MB_PTR(pos)->in_use) {
            return MB_PTR(pos);
        }
        pos++;
    }
    return NULL;
}

void write_to_disk(meta_bucket *mb)
{
#ifdef WRITE_TO_DISK
    void *buf;
    unsigned int dbindex;
    data_bucket *db;
    int rounds, off, i;

    buf = malloc(mb->s.st_size);
    rounds = mb->s.st_size / DBUCKET_BUF_SIZE;
    off = mb->s.st_size % DBUCKET_BUF_SIZE;
    dbindex = mb->first_dbucket;
    for (i = 0; i < rounds; i++) {
        db = get_dbucket_ptr(dbindex);
        memcpy(buf + i * DBUCKET_BUF_SIZE, db->buf, DBUCKET_BUF_SIZE);
        dbindex = db->next_ix;
    }
    if (off > 0) {
        db = get_dbucket_ptr(dbindex);
        memcpy(buf, db->buf, off);
    }
    int fd = real_open(mb->path, O_RDWR | O_CREAT | O_TRUNC, mb->s.st_mode);
    real_write(fd, buf, mb->s.st_size);
    real_close(fd);
    free(buf);
#else
    dlog(1, "[ufs: write_to_disk] would write %s\n", mb->path);
#endif
}

/* go through hash table, writing inconsistent files to disk */
void flush_write(void)
{
    dlog(2, "[flush_write]...\n");
    /* not handle delete now!!!!*/
    meta_bucket *mb = NULL;

    mb = traverse_next_bucket(mb);
    if (mb == NULL) {
        return;
    }
    do {
        if (!mb->consisent) {
            dlog(1, "[flush_write] modified: %s\n", mb->path);
            write_to_disk(mb);
        }
        else {
            //dlog(0, "[flush_write] consisent: %s\n", mb->path);
        }
    } while ((mb = traverse_next_bucket(mb)));
}

/** build absolute path -- add more complexity later
 ** for now only handles "./file" "file" "../file"
 ** path built and stored in (*abs_path) : return 0.
 ** If input is already absolute: don't copy anything,
 ** just return 1.
 ** -1 return: error.
 ** Does not support /../ or /./ in middle of string.
 */
int build_abs_path(char **abs_path_p, const char *path_in)
{
    int len = strlen(path_in);
    if (len > PATH_MAX) {
        dlog(1, "[ufs] build_abs_path: input path too long\n");
        return -1;
    }
    /* input path already absolute just let caller know */
    if (path_in[0] == '/' && path_in[1] != '.') {
        return 1;
    }
    /* only malloc if building path */
    *abs_path_p = malloc(PATH_MAX * sizeof(char));
    /* input is exactly ".." */
    if (len > 1 && path_in[0] == '.' && path_in[1] == '.' && path_in[2] == 0) {
        dlog(1, "[ufs: build_abs_path] '..' should be here\n");
        char tmp[PATH_MAX];
        EHANDLE("getcwd", getcwd(tmp, PATH_MAX), NULL);
        char *last_slash = strrchr(tmp, '/');
        *last_slash = 0;
        sprintf(*abs_path_p, "%s", tmp);
        return 0;
    }
    /* input starts with "../" */
    if (len > 1 && path_in[0] == '.' && path_in[1] == '.') {
        if (path_in[2] != '/') {
            dlog(1, "[ufs: build_abs_path] path_in[2] is %d\n", path_in[2]);
            dlog(1, "[ufs: build_abs_path] wrong format for input path: %s\n",
                 path_in);
            return -1;
        }
        char tmp[PATH_MAX];
        char *tp = &tmp[0];
        strcpy(tmp, path_in);
        tp++;
        tp++;
        tp++;
        EHANDLE("getcwd", getcwd(*abs_path_p, PATH_MAX), NULL);
        char *last_slash = strrchr(*abs_path_p, '/');
        last_slash++;
        *last_slash = 0;
        strcat(*abs_path_p, tp);
        dlog(1, "[ufs: build_abs_path] created abs path %s\n", *abs_path_p);
        return 0;
    }
    if (len > 1 && path_in[0] == '.' && path_in[1] == '/') {
        char tmp[PATH_MAX];
        char *tp = &tmp[0];
        strcpy(tmp, path_in);
        EHANDLE("getcwd", getcwd(*abs_path_p, PATH_MAX), NULL);
        tp++;
        tp++;
        strcat(*abs_path_p, "/");
        strcat(*abs_path_p, tp);
        dlog(1, "[ufs: build_abs_path] created abs path %s\n", *abs_path_p);
        return 0;
    }
    if ((path_in[0] != '/' && path_in[0] != '.') ||
        (path_in[0] == '.' && path_in[1] != '.' && path_in[1] != '/')) {
        EHANDLE("getcwd", getcwd(*abs_path_p, PATH_MAX), NULL);
        strcat(*abs_path_p, "/");
        strcat(*abs_path_p, path_in);
        dlog(1, "[ufs: build_abs_path] created abs path %s\n", *abs_path_p);
        return 0;
    }
    if (path_in[0] == '.' && path_in[1] == 0) {
        EHANDLE("getcwd", getcwd(*abs_path_p, PATH_MAX), NULL);
        strcat(*abs_path_p, "/");
        dlog(1, "[ufs: build_abs_path] created abs path %s\n", *abs_path_p);
        return 0;
    }
    /* other input formats not supported */
    return -1;
}

/** given a pointer to metainfo, stamp the corresponding stat struct time
 ** these can be OR'ed into "type"
 ** ST_ACS: set access time
 ** ST_MOD: set mod time
 ** ST_CHG: set status change time
 **/
int stamp(struct stat *s, int type)
{
    dlog(99, "[ufs: stamp] \n");
    if (!(type & (ST_ACS | ST_MOD | ST_CHG))) {
        UEHANDLE("wrong type");
    }

    if (type & ST_ACS) {
        clock_gettime(CLOCK_REALTIME_COARSE, &(s->st_atim));
    }
    if (type & ST_MOD) {
        clock_gettime(CLOCK_REALTIME_COARSE, &(s->st_mtim));
    }
    if (type & ST_CHG) {
        clock_gettime(CLOCK_REALTIME_COARSE, &(s->st_ctim));
    }

    return 0;
}

/* hash */
#ifdef USE_METROHASH
inline static uint64_t rotate_right(uint64_t v, unsigned k)
{
    return (v >> k) | (v << (64 - k));
}

inline static uint64_t read_u64(const void * const ptr)
{
    return (uint64_t)(*(const uint64_t *)(ptr));
}

inline static uint64_t read_u32(const void * const ptr)
{
    return (uint64_t)(*(const uint32_t *)(ptr));
}

inline static uint64_t read_u16(const void * const ptr)
{
    return (uint64_t)(*(const uint16_t *)(ptr));
}

inline static uint64_t read_u8 (const void * const ptr)
{
    return (uint64_t)(*(const uint8_t *)(ptr));
}

unsigned long metro_hash(const char *str)
{
	static const uint64_t k0 = 0xD6D018F5;
	static const uint64_t k1 = 0xA2AA033B;
	static const uint64_t k2 = 0x62992FC1;
	static const uint64_t k3 = 0x30BC5B29;
	static const uint64_t seed = 0x29328a2;

	size_t len = strlen(str);
	const uint8_t *ptr = (const uint8_t *)str;
	const uint8_t *end = ptr + len;

	uint64_t h = (seed + k2) * k0;
	if (len >= 32)
	{
		uint64_t v[4];
		v[0] = h;
		v[1] = h;
		v[2] = h;
		v[3] = h;

		do
		{
			v[0] += read_u64(ptr) * k0; ptr += 8; v[0] = rotate_right(v[0],29) + v[2];
			v[1] += read_u64(ptr) * k1; ptr += 8; v[1] = rotate_right(v[1],29) + v[3];
			v[2] += read_u64(ptr) * k2; ptr += 8; v[2] = rotate_right(v[2],29) + v[0];
			v[3] += read_u64(ptr) * k3; ptr += 8; v[3] = rotate_right(v[3],29) + v[1];
		}
		while (ptr <= (end - 32));

		v[2] ^= rotate_right(((v[0] + v[3]) * k0) + v[1], 37) * k1;
		v[3] ^= rotate_right(((v[1] + v[2]) * k1) + v[0], 37) * k0;
		v[0] ^= rotate_right(((v[0] + v[2]) * k0) + v[3], 37) * k1;
		v[1] ^= rotate_right(((v[1] + v[3]) * k1) + v[2], 37) * k0;
		h += v[0] ^ v[1];
	}
	if ((end - ptr) >= 16)
	{
		uint64_t v0 = h + (read_u64(ptr) * k2); ptr += 8; v0 = rotate_right(v0,29) * k3;
		uint64_t v1 = h + (read_u64(ptr) * k2); ptr += 8; v1 = rotate_right(v1,29) * k3;
		v0 ^= rotate_right(v0 * k0, 21) + v1;
		v1 ^= rotate_right(v1 * k3, 21) + v0;
		h += v1;
	}

	if ((end - ptr) >= 8)
	{
		h += read_u64(ptr) * k3; ptr += 8;
		h ^= rotate_right(h, 55) * k1;
	}

	if ((end - ptr) >= 4)
	{
		h += read_u32(ptr) * k3; ptr += 4;
		h ^= rotate_right(h, 26) * k1;
	}

	if ((end - ptr) >= 2)
	{
		h += read_u16(ptr) * k3; ptr += 2;
		h ^= rotate_right(h, 48) * k1;
	}

	if ((end - ptr) >= 1)
	{
		h += read_u8 (ptr) * k3;
		h ^= rotate_right(h, 37) * k1;
	}

	h ^= rotate_right(h, 28);
	h *= k0;
	h ^= rotate_right(h, 29);

	return (unsigned long)h;
}
#endif

unsigned long hash(const char *str)
{
#ifndef USE_METROHASH
	unsigned long hash = 5381;
	int c;
	while ((c = *str++)) {
		if (c < 0) {
			UEHANDLE("bad hash function input");
		}
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}
	return (hash % HASH_TABLE_SIZE);
#else
	return metro_hash(str) % HASH_TABLE_SIZE;
#endif
}
