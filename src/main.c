#define FUSE_USE_VERSION 30

#include "file.h"
#include "dir.h"

static struct fuse_operations cdcfs_oper = {
    .getattr        = cdcfs_getattr,
    .opendir        = cdcfs_opendir,
    .readdir        = cdcfs_readdir,
    .releasedir     = cdcfs_releasedir,
    .mkdir          = cdcfs_mkdir,
    .rmdir          = cdcfs_rmdir,
    .open           = cdcfs_open,
    .read           = cdcfs_read,
    .create         = cdcfs_create,
    .write          = cdcfs_write,
    .truncate       = cdcfs_truncate,
    .unlink         = cdcfs_unlink,
    .release        = cdcfs_release,
    .utime          = cdcfs_utime,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &cdcfs_oper, NULL);
}