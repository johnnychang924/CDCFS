#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <openssl/sha.h>
#include <string>
#include <set>
#include <vector>

#include "def.h"

std::string inode_to_path[MAX_INODE_NUM];
std::unordered_map<std::string, int> path_to_inode;
std::set<int> free_inode;
int cur_max_inode = -1;
std::unordered_map<std::string, u_int32_t> fp_store;

static int cdcfs_getattr(const char *path, struct stat *stbuf) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("get attr: " << path);

    res = lstat(full_path, stbuf);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_open(const char *path, struct fuse_file_info *fi) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("open: " << path);

    res = open(full_path, fi->flags);
    if (res == -1) {
        return -errno;
    }

    fi->fh = res;
    return 0;
}

static int cdcfs_release(const char *path, struct fuse_file_info *fi) {
    int res;
    DEBUG_MESSAGE("close: " << path);

    res = close(fi->fh);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_utime(const char *path, struct utimbuf *ubuf) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("utime: " << path);

    res = utime(full_path, ubuf);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    int res;
    DEBUG_MESSAGE("read: (" << path << ") offset: (" << offset << ") size: (" << size << ")");

    if (fi->fh < 0) {
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
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("create: " << path);
    
    res = creat(full_path, mode);
    if (res == -1) {
        return -errno;
    }
    fi->fh = res;
    return 0;
}

std::unordered_map<int, std::vector<char>> filebuffer;

static int cdcfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    DEBUG_MESSAGE("write: (" << path << ") offset: (" << offset << ") size: (" << size << ")");

    if (fi->fh < 0) {
        return -errno;
    }

    //if (offset % BLOCK_SIZE != 0) PRINT_WARNING("write: offset is not aligned to blocksize\n");
    for (int i = 0; i <= (int)(size / BLOCK_SIZE); i++) {
        char digest[SHA_DIGEST_LENGTH];
        SHA1((const unsigned char *)buf + i * BLOCK_SIZE, BLOCK_SIZE, (unsigned char *)digest);
        std::string digest_str(digest, SHA_DIGEST_LENGTH);
        if(fp_store.find(digest_str) != fp_store.end()) {
            DEBUG_MESSAGE("find duplicate");
        }
        else{
            fp_store[digest_str] = 0;
        }
    }
    
    return pwrite(fi->fh, buf, size, offset);
}

static int cdcfs_truncate(const char *path, off_t size) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("truncate: " << full_path);

    res = truncate(full_path, size);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_ftruncate(const char *path, off_t size, fuse_file_info *fi) {
    int res;
    DEBUG_MESSAGE("ftruncate: " << path);

    if  (fi == NULL) {
        return -errno;
    }

    res = ftruncate(fi->fh, size);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_unlink(const char *path) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("unlink: " << path);
    
    res = unlink(full_path);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_readlink(const char *path, char *buf, size_t size) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("readlink: " << full_path);

    res = readlink(full_path, buf, size - 1);
    if (res == -1) {
        return -errno;
    } 
    else {
        buf[res] = '\0';
        return 0;
    }
}

static int cdcfs_link(const char *oldpath, const char *newpath) {
    int res;
    char full_old[1024];
    char full_new[1024];
    snprintf(full_old, sizeof(full_old), "%s%s", BACKEND, oldpath);
    snprintf(full_new, sizeof(full_new), "%s%s", BACKEND, newpath);
    DEBUG_MESSAGE("link: " << full_new << " to " << full_old);
    
    res = link(full_old, full_new);
    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int cdcfs_symlink(const char *oldpath, const char *newpath) {
    int res;
    char full_old[1024];
    char full_new[1024];
    snprintf(full_old, sizeof(full_old), "%s%s", BACKEND, oldpath);
    snprintf(full_new, sizeof(full_new), "%s%s", BACKEND, newpath);
    DEBUG_MESSAGE("symlink: " << full_new << " to " << full_old);

    res = symlink(full_old, full_new);
    if (res == -1) {
        return -errno;
    }
    return 0;
}