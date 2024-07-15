#define FUSE_USE_VERSION 30

#include "file.h"
#include "dir.h"

static struct fuse_operations cdcfs_oper = {
    .getattr        = cdcfs_getattr,
    .mkdir          = cdcfs_mkdir,
    .unlink         = cdcfs_unlink,
    .rmdir          = cdcfs_rmdir,
    .truncate       = cdcfs_truncate,
    .utime          = cdcfs_utime,
    .open           = cdcfs_open,
    .read           = cdcfs_read,
    .write          = cdcfs_write,
    .release        = cdcfs_release,
    .opendir        = cdcfs_opendir,
    .readdir        = cdcfs_readdir,
    .releasedir     = cdcfs_releasedir,
    .create         = cdcfs_create,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &cdcfs_oper, NULL);
}