#define FUSE_USE_VERSION 30

#include <filesystem>
#include <fstream>
#include "file.h"
#include "dir.h"

static void cdcfs_leave(void *param){
    PRINT_MESSAGE("\n----------------------------------------leaving CDCFS !!!----------------------------------------");
    PRINT_MESSAGE("total write size:" << (float)total_write_size / 1000000000 << "GB");
    PRINT_MESSAGE("total dedup rate:" << (float)total_dedup_size / total_write_size * 100 << "%");
    // output the mapping table to a file
    #ifdef MAPPING_OUTPUT_PATH
        std::ofstream mapping_output(MAPPING_OUTPUT_PATH);
        for (const auto &[file_path, iNum] : path_to_iNum) {
            mapping_output << "file: " << file_path << std::endl;
            mapping_table_entry *entry = &mapping_table[iNum];
            for (uint64_t group_id = 0; group_id < (uint64_t)entry->group_pos.size(); group_id++) {
                if (entry->group_pos[group_id]->iNum == iNum){
                    mapping_output << "noDedup: " << entry->group_pos[group_id]->start_byte << " " << entry->group_pos[group_id]->group_length << std::endl;
                }
                else{
                    mapping_output << "dedup: " << iNum_to_path[entry->group_pos[group_id]->iNum] << " "<< entry->group_pos[group_id]->start_byte  << " " << entry->group_pos[group_id]->group_length << std::endl;
                }
            }
        }
        mapping_output.close();
    #endif
    // output the read request to a file
    #ifdef READ_REQ_OUTPUT_PATH
        std::ofstream read_req_output(READ_REQ_OUTPUT_PATH);
        for (int idx = 0; idx < rd_req_count; idx++){
            read_req_output << rd_req[idx].inum << " " << rd_req[idx].off << " " << rd_req[idx].size << std::endl;
        }
    #endif
}

static struct fuse_operations cdcfs_oper = {
    .getattr        = cdcfs_getattr,
    .readlink       = cdcfs_readlink,
    .mkdir          = cdcfs_mkdir,
    //.unlink         = cdcfs_unlink,
    .rmdir          = cdcfs_rmdir,
    .symlink        = cdcfs_symlink,
    .link           = cdcfs_link,
    //.truncate       = cdcfs_truncate,
    .utime          = cdcfs_utime,
    .open           = cdcfs_open,
    .read           = cdcfs_read,
    .write          = cdcfs_write,
    .release        = cdcfs_release,
    .opendir        = cdcfs_opendir,
    .readdir        = cdcfs_readdir,
    .releasedir     = cdcfs_releasedir,
    .destroy        = cdcfs_leave,
    .create         = cdcfs_create,
    .ftruncate      = cdcfs_ftruncate,
};

int main(int argc, char *argv[]) {
    // remove every file in backend directory.
    bool show_confirm = false;
    char replay;
    for (const auto& entry : std::filesystem::directory_iterator(BACKEND)){
        if (!show_confirm){
            std::cout << "WARNING: BACKEND directory is not empty, all files in it will be removed!![y|n]";
            std::cin >> replay;
            show_confirm = true;
            if (replay == 'n') return 0;
        }
        std::filesystem::remove_all(entry.path());
    }
    // init CDCFS data structure
    PRINT_MESSAGE("----------------------------------------entering CDCFS !!----------------------------------------");
    for (INUM_TYPE iNum = 0; iNum < MAX_INODE_NUM - 1; ++iNum) {
        free_iNum.insert(iNum);
    }
    for(FILE_HANDLER_INDEX_TYPE file_handler = 0; file_handler < MAX_FILE_HANDLER - 1; ++file_handler){
        free_file_handler.insert(file_handler);
    }
    // init fastcdc engine
    cdc = fastcdc_init(0, BLOCK_SIZE, MAX_GROUP_SIZE);
    ctx = &cdc;
    // start CDCFS
    return fuse_main(argc, argv, &cdcfs_oper, NULL);
}