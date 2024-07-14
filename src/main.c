#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#include "def.h"

static const char *backend = "/mnt/btrfs";

static int cdcfs_getattr(const char *path, struct stat *stbuf) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("get attr: %s\n", full_path);

    res = lstat(full_path, stbuf);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int cdcfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    DIR *dp;
    struct dirent *de;
    char full_path[1024];

    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("read dir: %s\n", full_path);

    dp = opendir(full_path);
    if (dp == NULL) {
        return -errno;
    }

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0)) {
            break;
        }
    }

    closedir(dp);
    return 0;
}

static int cdcfs_open(const char *path, struct fuse_file_info *fi) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("open: %s\n", full_path);

    res = open(full_path, fi->flags);
    if (res == -1) {
        return -errno;
    }

    close(res);
    return 0;
}

static int cdcfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    int fd;
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("read: %s offset:%ld size:%ld\n", full_path, offset, size);

    fd = open(full_path, O_RDONLY);
    if (fd == -1) {
        return -errno;
    }

    res = pread(fd, buf, size, offset);
    if (res == -1) {
        res = -errno;
    }

    close(fd);
    return res;
}

static struct fuse_operations cdcfs_oper = {
    .getattr    = cdcfs_getattr,
    .readdir    = cdcfs_readdir,
    .open       = cdcfs_open,
    .read       = cdcfs_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &cdcfs_oper, NULL);
}