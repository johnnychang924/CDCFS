#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "def.h"

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

static int cdcfs_open(const char *path, struct fuse_file_info *fi) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("open: %s\n", full_path);

    res = open(full_path, fi->flags);
    if (res == -1) {
        return -errno;
    }

    fi->fh = res;
    return 0;
}

static int cdcfs_release(const char *path, struct fuse_file_info *fi) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("close: %s\n", full_path);

    res = close(fi->fh);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_utime(const char *path, struct utimbuf *ubuf) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("utime: %s\n", full_path);

    res = utime(full_path, ubuf);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("read: %s offset:%ld size:%ld\n", full_path, offset, size);

    if (fi->fh == (long unsigned int)-1) {
        return -errno;
    }

    res = pread(fi->fh, buf, size, offset);
    if (res == -1) {
        return -errno;
    }
    return res;
}

static int cdcfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("create: %s\n", full_path);
    
    res = creat(full_path, mode);
    if (res == -1) {
        return -errno;
    }
    fi->fh = res;
    return 0;
}

static int cdcfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("write: %s offset:%ld size:%ld\n", full_path, offset, size);
    if (fi->fh == (long unsigned int)-1) {
        return -errno;
    }
    res = pwrite(fi->fh, buf, size, offset);
    if (res == -1) {
        return -errno;
    }
    return res;
}

static int cdcfs_truncate(const char *path, off_t size) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("truncate: %s size:%ld\n", full_path, size);
    if  (size == -1) {
        return -errno;
    }
    res = truncate(full_path, size);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_unlink(const char *path) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", backend, path);
    DEBUG_MESSAGE("unlink: %s\n", full_path);
    res = unlink(full_path);
    if (res == -1) {
        return -errno;
    }
    return 0;
}