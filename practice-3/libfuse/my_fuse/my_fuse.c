/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/

/** @file
 *
 * minimal example filesystem using high-level API
 *
 * Compile with:
 *
 *     gcc -Wall my_fuse.c `pkg-config fuse3 --cflags --libs` -o fuse
 *
 * ## Source code ##
 * \include hello.c
 *
 * ref: https://blog.csdn.net/stayneckwind2/article/details/82876330
 */


#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>

#include <stdlib.h>
#include <pthread.h>
#include "rbtree.h"
#include <sys/statvfs.h>
#include <sys/types.h>


#define U_ATIME (1<<0)
#define U_CTIME (1<<1)
#define U_MTIME (1<<2)
#define U_ALL   (U_ATIME | U_CTIME | U_MTIME)

#define BLOCKSIZE (4096)
#define MAX_NAME (255)
#define MAX_INODE (1<<20)
#define MAX_BLOCKS (1<<20)
#define SUCCESS (0)

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
struct options {     // wrap filename & contents together
    const char *filename;
    const char *contents;
    int show_help;
}options[1000];

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
        OPTION("--name=%s", filename),
        OPTION("--contents=%s", contents),
        OPTION("-h", show_help),
        OPTION("--help", show_help),
        FUSE_OPT_END
};

// global read/write lock of file
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER, lock_write = PTHREAD_MUTEX_INITIALIZER;
static struct rb_root root = RB_ROOT;
static struct statvfs stat_infos;  // stat info

FILE *debug_fp;
const char *debug_fp_name = "output.txt";

// file info
static struct memfs_file {
    char *path;
    struct options *data;    // filename & contents

    struct stat file_stat;
    struct rb_node node;
    pthread_mutex_t lock;
    int attr;               // 0: file, 1: directory
};

static inline void __do_update_times(struct memfs_file *pf, int which) {
    time_t now = time(NULL);
    if (which & U_ATIME) {
        pf->file_stat.st_atime = now;
    }
    if (which & U_CTIME) {
        pf->file_stat.st_ctime = now;
    }
    if (which & U_MTIME) {
        pf->file_stat.st_mtime = now;
    }
}

static inline const char *get_file_name(const char *str) {
    // str: /mnt/d/test
    // return: test
    const char *lastSlash = strrchr(str, '/');  // 从右边开始第一个出现的位置
    if (lastSlash == NULL) return str;
    else return lastSlash + 1;
}

//---------------------------------------------------------------basic operations for red_black tree

static int __insert(struct rb_root *root, struct memfs_file *pf) {
    struct rb_node **new = &(root->rb_node), *parent = NULL;

    /* Figure out where to put new node */
    while (*new) {
        struct memfs_file *this = container_of(*new, struct memfs_file, node);
        int result = strcmp(pf->path, this->path);
        parent = *new;
        if (result < 0) {
            new = &((*new)->rb_left);
        } else if (result > 0) {
            new = &((*new)->rb_right);
        } else {
            fprintf(debug_fp, "%s is exist!!!\n", pf->path);
            return -1;
        }
    }

    /* Add new node and re_balance tree. */
    rb_link_node(&pf->node, parent, new);
    rb_insert_color(&pf->node, root);
    return 0;
}

static struct memfs_file *__search(struct rb_root *root, const char *path) {
    struct memfs_file *pf = NULL;
    struct rb_node *node = root->rb_node;

    while (node) {
        pf = container_of(node, struct memfs_file, node);

        int result = strcmp(path, pf->path);
        if (result < 0) {
            node = node->rb_left;
        } else if (result > 0) {
            node = node->rb_right;
        } else {
            return pf;
        }
    }

//  fprintf(debug_fp, "%s not found!!!\n", path);
  return NULL;
}

static inline void __free(struct memfs_file *pf) {
    if (pf->data) {
        free(pf->data);
    }
    if (pf->path) {
        free(pf->path);
    }
    free(pf);
}

static int __delete(struct rb_root *root, const char *path) {
    struct memfs_file *pf = __search(root, path);
    if (!pf) {
        return -1;
    }

    int blocks = pf->file_stat.st_blocks;
    rb_erase(&pf->node, root);
    __free(pf);

    return blocks;
}

//-----------------------------------------------------Overwrite some function in FUSE

static struct memfs_file *__new(const char *path, mode_t mode, int attr) {
    // prevent illegal insert
    if (__search(&root, path)) return -EEXIST;

    struct options *new_option = malloc(sizeof(struct options));
    new_option->filename = strdup(get_file_name(path));
    new_option->contents = strdup("");
    struct memfs_file *new_node = malloc(sizeof(struct memfs_file));
    new_node->path = strdup(path);
    new_node->data = new_option;
    new_node->file_stat.st_mode = mode;
    new_node->attr = attr;

    __insert(&root, new_node);
    return new_node;
}

static void *my_init(struct fuse_conn_info *conn,
                     struct fuse_config *cfg) {
    (void) conn;
    fprintf(debug_fp, "%s\n", __FUNCTION__);

    cfg->kernel_cache = 1;

    stat_infos.f_bsize = BLOCKSIZE;                 /* Filesystem block size */
    stat_infos.f_frsize = BLOCKSIZE;                 /* Fragment size */
    stat_infos.f_blocks = MAX_BLOCKS;                /* Size of fs in f_frsize units */
    stat_infos.f_bfree = MAX_BLOCKS;                /* Number of free blocks */
    stat_infos.f_bavail = MAX_BLOCKS;                /* Number of free blocks for unprivileged users */
    stat_infos.f_files = MAX_INODE;                 /* Number of inodes */
    stat_infos.f_ffree = MAX_INODE;                 /* Number of free inodes */
    stat_infos.f_favail = MAX_INODE;                 /* Number of free inodes for unprivileged users */
    stat_infos.f_fsid = 0x0123456701234567;        /* Filesystem ID */
//      .f_flags   = 0,                         /* Mount flags */
    stat_infos.f_namemax = MAX_NAME;                  /* Maximum filename length */

    struct memfs_file *pf = __new("/", S_IFDIR | 0755, 1);      // add root path
    pf->file_stat.st_gid = getgid();
    pf->file_stat.st_uid = getuid();
    __do_update_times(pf, U_ALL);

    return NULL;
}

static int my_getattr(const char *path, struct stat *stbuf,
                      struct fuse_file_info *fi) {
    (void) fi;
    int res = 0;

    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);
    memset(stbuf, 0, sizeof(struct stat));

    pthread_mutex_lock(&lock);

    struct memfs_file *pf = __search(&root, path);
    if (!pf) {
        pthread_mutex_unlock(&lock);
        return -ENOENT;
    } else {
        *stbuf = pf->file_stat;
    }

    pthread_mutex_unlock(&lock);

    return res;
}

/*
 * @parent - "/tmp"
 * @path   - "/tmp/1.txt"
 *
 * return: /1.txt
 */
static inline const char *__is_parent(const char *parent, const char *path) {
    if (parent[1] == '\0' && parent[0] == '/' && path[0] == '/') {
        return path;
    }

    while (*parent != '\0' && *path != '\0' && *parent == *path) {
        ++parent, ++path;
    }
    return (*parent == '\0' && *path == '/') ? path : NULL;
}

static int __do_readdir(const char *dirname, void *buf, fuse_fill_dir_t filler) {
    struct rb_node *node = NULL;
    struct memfs_file *pentry = __search(&root, dirname);
    if (!pentry) {      // node not found
        return -ENOENT;
    } else if (pentry->attr != 1) {     // not a directory
        return -ENOTDIR;                // (!S_ISDIR(pentry->file_stat.st_mode))
    }

    for (node = rb_next(&pentry->node); node; node = rb_next(node)) {
        const struct memfs_file *pf = rb_entry(node, struct memfs_file, node);
        const char *basename = __is_parent(dirname, pf->path);

        if (!basename) {
            break;
        } else if (strchr(basename + 1, '/')) {  // find '/' after basename
            // only return first child
            continue;
        }
        filler(buf, basename + 1, &(pf->file_stat), 0, 0);
        fprintf(debug_fp, " readdir: %10s, path: %10s\n", basename, pf->path);
    }

    return 0;
}

static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    int res = 0;
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    filler(buf, ".", NULL, 0, 0);
    // 不是根目录 "/", 可以返回到上级目录
    if (strcmp(path, "/") != 0) {
        filler(buf, "..", NULL, 0, 0);
    }

    pthread_mutex_lock(&lock);
    res = __do_readdir(path, buf, filler);
    pthread_mutex_unlock(&lock);

    return res;
}

// check whether we can access the file in the path
static int my_access(const char *path, int mask) {
    int res = 0;
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    pthread_mutex_lock(&lock);
    struct memfs_file *pf = __search(&root, path);
    if (!pf) {
        res = -ENOENT;
    }
    pthread_mutex_unlock(&lock);

    return res;
}

// get file info
static int my_statfs(const char *path, struct statvfs *stbuf) {
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);
    *stbuf = stat_infos;
    return 0;
}

static int my_open(const char *path, struct fuse_file_info *fi) {
    int res = 0;
    struct memfs_file *pf = NULL;
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    pthread_mutex_lock(&lock);
    pf = __search(&root, path);
    if (!pf) {
        if ((fi->flags & O_ACCMODE) == O_RDONLY ||
            !(fi->flags & O_CREAT)) {   // read-only
            fprintf(debug_fp, "can't modify read-only file at %s\n", path);
            pthread_mutex_unlock(&lock);
            return -ENOENT;
        }
        pf = __new(path, S_IFREG | 0755, 0);
    } else {
        if (S_ISDIR(pf->file_stat.st_mode) || pf->attr == 1) {  // can't open a directory
            pthread_mutex_unlock(&lock);
            return -EISDIR;
        }
    }

    fi->fh = (unsigned long) pf;
    pthread_mutex_unlock(&lock);
    return res;
}

static int my_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    size_t len;
    (void) fi;

    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    struct memfs_file *pf = __search(&root, path);
    if (!pf || strcmp(get_file_name(path), pf->data->filename) != 0) { // path here may contain parent directory, like /sss/a.txt
        fprintf(debug_fp, "can't find file at %s\n", path);
        return -ENOENT;
    }

    len = strlen(pf->data->contents);
    if (offset < len) {
        if (offset + size > len) size = len - offset;
        memcpy(buf, pf->data->contents + offset, size);
    } else {
        size = 0;
    }

    __do_update_times(pf, U_ATIME);     // access time modified

    return size;
}

// there are some problems in the blog...
static int my_write(const char *path,
                    const char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    struct memfs_file *pf = (struct memfs_file *) fi->fh;
    fprintf(debug_fp, "%s: %s, size: %zd\n", __FUNCTION__, path, size);

    // Check whether the file was opened for reading
//    blkcnt_t req_blocks = (offset + size + BLOCKSIZE - 1) / BLOCKSIZE;

    pthread_mutex_lock(&pf->lock);
    // writing buf to contents
    size_t len = strlen(pf->data->contents);
    void *new_data = realloc(pf->data->contents, sizeof(char) * (len + size + 1));
    if (!new_data) {
        pthread_mutex_unlock(&pf->lock);
        return -ENOMEM;      // memory is not enough
    }
    pf->data->contents = new_data;
    strcat(pf->data->contents, buf);    // connect new buf
    strcat(pf->data->contents, "\0");

    // write to another file to realize chatting...
    // example: echo Hello > bot2/bot1, then we should find bot1/bot2 with Hello
    char *tmp;
    if ((tmp = strchr(path + 1, '/'))) {
        // only have 2 levels?: path = /bot2/bot1
        // tmp = /bot1
        // new_path = /bot1/bot2
        char *new_path = malloc(sizeof(char) * strlen(path));
        strcpy(new_path, tmp);
        strncat(new_path, path, tmp - path);

        struct memfs_file *pf2 = __search(&root, new_path);
        if (!pf2) {
            pthread_mutex_unlock(&pf->lock);
            return -ENONET;
        }

        size_t len2 = strlen(pf2->data->contents);
        void *new_data_2 = realloc(pf2->data->contents, sizeof(char) * (len2 + size + 1));
        if (!new_data_2) {
            pthread_mutex_unlock(&pf->lock);
            return -ENOMEM;      // memory is not enough
        }
        pf2->data->contents = new_data_2;
        strcat(pf2->data->contents, buf);    // connect new buf
        strcat(pf2->data->contents, "\0");
    }

    // Update file size if necessary
    off_t minsize = offset + size;
    if (minsize > pf->file_stat.st_size) {
        pf->file_stat.st_size = minsize;
    }
    pthread_mutex_unlock(&pf->lock);

    __do_update_times(pf, U_ALL);
    return size;
}

static int my_release(const char *path, struct fuse_file_info *fi) {
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);
    return 0;
}

static int my_unlink(const char *path) {
    // 删除文件时调用
    int res = 0, blocks = 0;
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    pthread_mutex_lock(&lock);

    blocks = __delete(&root, path);
    if (blocks < 0) {
        pthread_mutex_unlock(&lock);
        return -ENOENT;
    }

    stat_infos.f_bfree = stat_infos.f_bavail += blocks;
    stat_infos.f_favail = ++stat_infos.f_ffree;

    pthread_mutex_unlock(&lock);

    return res;
}

static int my_mkdir(const char *path, mode_t mode) {
    int res = 0;
    struct memfs_file *pf = NULL;

    pthread_mutex_lock(&lock);
    pf = __new(path, S_IFDIR | mode, 1);
    if (!pf) {
        pthread_mutex_unlock(&lock);
        return -ENOMEM;
    }

    __do_update_times(pf, U_ALL);
   pthread_mutex_unlock(&lock);

    return res;
}

static int my_rmdir(const char *path) {
    int res = 0;
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    pthread_mutex_lock(&lock);
    if (__delete(&root, path) < 0) {
        res = -ENOENT;
    }
    pthread_mutex_unlock(&lock);
    return res;
}

static int my_mknod(const char *path, mode_t mode, dev_t rdev) {
    int res = 0;
    struct memfs_file *pf = __search(&root, path);
    if (pf) return -EEXIST;
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    pthread_mutex_lock(&lock);
    pf = __new(path, mode, 0);
    if (!pf) {
        pthread_mutex_unlock(&lock);
        return -ENOMEM;
    }

    // create another file to chat
    char *tmp;
    if ((tmp = strchr(path + 1, '/'))) {
        // only have 2 levels?: path = /bot2/bot1
        // tmp = /bot1
        // new_path = /bot1/bot2
        char *new_path = malloc(sizeof(char) * strlen(path));
        strcpy(new_path, tmp);
        strncat(new_path, path, tmp - path);

        struct memfs_file *pf2 = __search(&root, new_path);
        if (pf2) {
            pthread_mutex_unlock(&lock);
            return -EEXIST;
        }
       pf = __new(new_path, pf->file_stat.st_mode, 0);
    }

    stat_infos.f_favail = --stat_infos.f_ffree;
    // __do_update_times(pf, U_ALL);

    pthread_mutex_unlock(&lock);
    return res;
}

static int my_utimes(const char *path, const struct timespec tv[2],
                     struct fuse_file_info *fi) {
    struct memfs_file *pf = __search(&root, path);
    fprintf(debug_fp, "%s: %s\n", __FUNCTION__, path);

    if (!pf) {
        pf = __new(path, S_IFREG | 0644, 0);
    }

    __do_update_times(pf, U_ALL);
    return 0;
}


// the set of fuse operations(supporting command)
static const struct fuse_operations my_oper = {
        .init           = my_init,
        .getattr        = my_getattr,
        .readdir        = my_readdir,
        .access         = my_access,

        .open           = my_open,
        .read           = my_read,
        .write          = my_write,
        .release        = my_release,

        .mkdir          = my_mkdir,
        .rmdir          = my_rmdir,

        .mknod          = my_mknod,
        .unlink         = my_unlink,

        .statfs         = my_statfs,

        .utimens        = my_utimes,
};

static void show_help(const char *progname) {
    printf("usage: %s [options] <mountpoint>\n\n", progname);
    printf("File-system specific options:\n"
           "    --name=<s>          Name of the \"hello\" file\n"
           "                        (default: \"hello\")\n"
           "    --contents=<s>      Contents \"hello\" file\n"
           "                        (default \"Hello, World!\\n\")\n"
           "\n");
}

int main(int argc, char *argv[]) {
    debug_fp = fopen(debug_fp_name, "w");

    int ret;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    /* Set defaults -- we have to use strdup so that
       fuse_opt_parse can free the defaults if other
       values are specified */
//    options.filename = strdup("hello");
//    options.contents = strdup("Hello World!\n");

    /* Parse options */
    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
        return 1;

    /* When --help is specified, first print our own file-system
       specific help text, then signal fuse_main to show
       additional help (by adding `--help` to the options again)
       without usage: line (by setting argv[0] to the empty
       string) */
//    if (options.show_help) {
//        show_help(argv[0]);
//        assert(fuse_opt_add_arg(&args, "--help") == 0);
//        args.argv[0][0] = '\0';
//    }

    ret = fuse_main(args.argc, args.argv, &my_oper, NULL);
    fuse_opt_free_args(&args);

    fclose(debug_fp);
    return ret;
}
