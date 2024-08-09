#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#include "def.h"

static int cdcfs_opendir(const char *path, struct fuse_file_info *fi) {
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[open dir]" << path);

    DIR *dp = opendir(full_path);
    if (dp == NULL) {
        return -errno;
    }
    fi->fh = (uint64_t) dp;
    return 0;
}

static int cdcfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    DIR *dp;
    struct dirent *de;

    DEBUG_MESSAGE("[read dir]" << path);

    dp = (DIR *)fi->fh;
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

    return 0;
}

static int cdcfs_releasedir(const char *path, struct fuse_file_info *fi) {
    int res;

    DEBUG_MESSAGE("[release dir]" << path);

    res = closedir((DIR *) fi->fh);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_mkdir(const char *path, mode_t mode) {
    int res;
    char full_path[1024];

    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[create dir]" << path);
    res = mkdir(full_path, mode);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_rmdir(const char *path) {
    int res;
    char full_path[1024];

    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("[remove dir]" << path);
    res = rmdir(full_path);
    if (res == -1) {
        return -errno;
    }
    return 0;
}