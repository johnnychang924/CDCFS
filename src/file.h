#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <map>
#include <openssl/sha.h>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include "def.h"

struct group_addr{
    INUM_TYPE iNum;
    uint16_t file_st_blk;   // block is the smallest dedup grain
    uint16_t file_blk_cnt;
};

struct mapping_table_entry{
    std::vector<unsigned long> group_id;        // Group ID of every block
    std::vector<unsigned long> group_offset;    // the start byte in file of each group
    std::vector<group_addr *> group_pos;        // The position of every Group
    std::vector<FP_TYPE> fp_list;               // fingerprint of every block
    unsigned long actual_size_in_disk;          // current in disk block(BLOCK_SIZE) number (after dedup)
};

struct buffer_entry{
    off_t st_byte;   // which bytes to start
    uint16_t byte_cnt;  // how many bytes in buffer
    char *buf = NULL;
};

PATH_TYPE iNum_to_path[MAX_INODE_NUM];
std::unordered_map<PATH_TYPE, INUM_TYPE> path_to_iNum;
std::set<INUM_TYPE> free_iNum;
INUM_TYPE iNum_top = 0;

std::unordered_map<uint64_t, INUM_TYPE> get_iNum_by_fh;             // get iNum by file handler (faster than get by file path)
std::unordered_map<FP_TYPE, group_addr *> fp_store;
buffer_entry file_buffer[MAX_INODE_NUM];                            // buffer data util it's full
mapping_table_entry mapping_table[MAX_INODE_NUM];

// a simple write lock (I give up, maybe not important?)
// pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;

inline INUM_TYPE get_inum(PATH_TYPE path_str){
    auto it = path_to_iNum.find(path_str);
    if (it != path_to_iNum.end()){          // already written file, return old inode number
        return it->second;
    }
    else{                                   // new coming file, assign a new inode number
        INUM_TYPE new_iNum;
        if (free_iNum.empty()){
            new_iNum = iNum_top++;
        }
        else{
            new_iNum = *free_iNum.begin();
            free_iNum.erase(free_iNum.begin());
        }
        path_to_iNum[path_str] = new_iNum;
        iNum_to_path[new_iNum] = path_str;
        return new_iNum;
    }
}

inline INUM_TYPE prepare_write(const char *path, const uint64_t file_handler){
    PATH_TYPE path_str(path);
    INUM_TYPE iNum = get_inum(path_str);
    get_iNum_by_fh[file_handler] = iNum;    // cahce the iNum by file handler
    if (file_buffer[iNum].buf != NULL) 
        PRINT_WARNING("Critical error: trying to allocate file buffer when old buffer is already exist!!");
    file_buffer[iNum] = {
        .st_byte = 0,
        .byte_cnt = 0,
        .buf = new char[BLOCK_SIZE],        // remember to delete it when release
    };
    return iNum;
}

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
    INUM_TYPE iNum = prepare_write(path, fi->fh);
    mapping_table[iNum] = {
        .group_id = std::vector<unsigned long>(),
        .group_pos = std::vector<group_addr *>(),
        .fp_list = std::vector<FP_TYPE>(),
        .actual_size_in_disk = 0
    };
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
    DEBUG_MESSAGE("open: " << "(file handler)" << fi->fh);
    prepare_write(path, fi->fh);
    return 0;
}

static int cdcfs_release(const char *path, struct fuse_file_info *fi) {
    int res;
    DEBUG_MESSAGE("close: " << path);

    auto it = get_iNum_by_fh.find(fi->fh);
    INUM_TYPE iNum = it->second;
    get_iNum_by_fh.erase(it);

    // write back file buffer
    int write_back_ptr = 0;
    DEBUG_MESSAGE("release: write back: " << file_buffer[iNum].byte_cnt << " to " << path);
    while (write_back_ptr < file_buffer[iNum].byte_cnt){
        // not implement content chunking yet, use fixed sized
        int cut_pos = file_buffer[iNum].byte_cnt;

        char cur_fp[SHA_DIGEST_LENGTH];
        SHA1((const unsigned char *)file_buffer[iNum].buf + write_back_ptr, cut_pos, (unsigned char *)cur_fp);
        FP_TYPE new_fp(cur_fp, SHA_DIGEST_LENGTH);

        auto fp_store_iter = fp_store.find(new_fp);
        if (fp_store_iter != fp_store.end()){   // found
            DEBUG_MESSAGE("write: found duplicate group!!");
            mapping_table[iNum].group_pos.push_back(fp_store_iter->second);
            mapping_table[iNum].group_offset.push_back(file_buffer[iNum].st_byte + write_back_ptr);
            int blk_count = std::ceil((float)cut_pos / BLOCK_SIZE);
            for(int i = 0; i < blk_count; i++){
                mapping_table[iNum].group_id.push_back(mapping_table[iNum].group_pos.size() - 1);
            }
        }
        else{                                   // not found
            group_addr *new_group_addr = new group_addr;
            new_group_addr->iNum = iNum;
            new_group_addr->file_st_blk = mapping_table[iNum].actual_size_in_disk / BLOCK_SIZE;
            new_group_addr->file_blk_cnt = std::ceil((float)cut_pos / BLOCK_SIZE);
            int res = pwrite(fi->fh, file_buffer[iNum].buf + write_back_ptr, cut_pos, mapping_table[iNum].actual_size_in_disk);
            DEBUG_MESSAGE("write: (cut pos) " << cut_pos << " (actual_size_in_disk) " << mapping_table[iNum].actual_size_in_disk);
            if (res == -1){
                PRINT_WARNING("write: write back to disk failed!!");
                delete new_group_addr;
                return -errno;
            }
            int blk_count = std::ceil((float)cut_pos / BLOCK_SIZE);
            mapping_table[iNum].actual_size_in_disk += cut_pos;
            mapping_table[iNum].group_pos.push_back(new_group_addr);
            mapping_table[iNum].group_offset.push_back(file_buffer[iNum].st_byte + write_back_ptr);
            for(int i = 0; i < blk_count; i++){
                mapping_table[iNum].group_id.push_back(mapping_table[iNum].group_pos.size() - 1);
            }
            fp_store[new_fp] = new_group_addr;
        }

        write_back_ptr += cut_pos;
    }
    file_buffer[iNum].byte_cnt = 0;
    file_buffer[iNum].buf = NULL;

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
    //DEBUG_MESSAGE("read: " << "(file handler)" << fi->fh);
    INUM_TYPE iNum = get_iNum_by_fh[fi->fh];
    unsigned long start_group_id = mapping_table[iNum].group_id[offset / BLOCK_SIZE];               // included the first block
    unsigned long final_group_id = mapping_table[iNum].group_id[(offset + size - 1) / BLOCK_SIZE];  // included the last block

    char *buf_ptr = buf;
    off_t cur_offset = offset;
    std::map<INUM_TYPE, uint64_t> fh_cache;     // cache other file handlers by inode number
    int total_read_byes = 0;
    size_t end_offset = offset + size;

    for(unsigned long curr_group_id = start_group_id; curr_group_id <= final_group_id; curr_group_id++) {
        //DEBUG_MESSAGE("read group: " << curr_group_id);
        off_t start_offset_in_group = cur_offset - mapping_table[iNum].group_offset[curr_group_id];
        size_t group_size = mapping_table[iNum].group_pos[curr_group_id]->file_blk_cnt * BLOCK_SIZE;
        off_t end_offset_in_group = std::min(mapping_table[iNum].group_offset[curr_group_id] + group_size, end_offset) - mapping_table[iNum].group_offset[curr_group_id];
        off_t offset_in_file = mapping_table[iNum].group_pos[curr_group_id]->file_st_blk * BLOCK_SIZE;
        //DEBUG_MESSAGE("offset_in_group" << offset_in_group << "offset in file" << offset_in_file << "group size" << group_size);
        INUM_TYPE group_iNum = mapping_table[iNum].group_pos[curr_group_id]->iNum;
        if (group_iNum == iNum){    // not being dedup group
            //DEBUG_MESSAGE("read: (iNum)" << (int)iNum << " (offset)" <<  offset_in_file + offset_in_group << "(size)" << group_size - offset_in_group);
            res = pread(fi->fh, buf_ptr, end_offset_in_group - start_offset_in_group, offset_in_file + start_offset_in_group);
        }
        else{                       // being dedup group
            if (fh_cache.find(group_iNum) == fh_cache.end()) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, iNum_to_path[group_iNum].c_str());
                fh_cache[group_iNum] = open(full_path, O_RDONLY | O_DIRECT);
            }
            res = pread(fh_cache[group_iNum], buf_ptr, end_offset_in_group - start_offset_in_group, offset_in_file + start_offset_in_group);
        }
        buf_ptr += end_offset_in_group - start_offset_in_group;
        cur_offset += end_offset_in_group - start_offset_in_group;
        if (res == -1) {
            goto fail;
        }
        total_read_byes += res;
    }

    return total_read_byes;

fail:
    for (auto it = fh_cache.begin(); it!= fh_cache.end(); ++it) {
        close(it->second);
    }
    return -errno;
}

/*
* Write buf into disk
* 1. get file buffer in memory
*   1.1 if file buffer exists and offset is not continuous, write back to disk.
*   1.2 if file buffer exists and offset is continuous, write to memory directly.
* 2. if file buffer FULL use fastCDC to decide chunking.
* 3. hashing the chunk.
*/
static int cdcfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    DEBUG_MESSAGE("write: (" << path << ") offset: (" << offset << ") size: (" << size << ")");

    if (fi->fh < 0)
        return -errno;

    INUM_TYPE iNum = get_iNum_by_fh[fi->fh];
    buffer_entry *in_buffer_data = &file_buffer[iNum];

    if (in_buffer_data->byte_cnt > 0 && in_buffer_data->st_byte + in_buffer_data->byte_cnt != offset) {     // file buffer is not empty and current write is not continous.
        PRINT_WARNING("write: detect not continous write in write buffer");
        return -errno;
    }

    if (offset < (long int)mapping_table[iNum].actual_size_in_disk + in_buffer_data->byte_cnt) {
        PRINT_WARNING("write: currently not support data update.");
        return -errno;
    }

    if (in_buffer_data->byte_cnt == 0) in_buffer_data->st_byte = offset;

    size_t less_size = size;
    char * curr_buf_ptr = (char *)buf;
    while(less_size > 0){
        bool can_fill_buffer = in_buffer_data->byte_cnt + less_size >= MAX_GROUP_SIZE;
        if (can_fill_buffer){
            DEBUG_MESSAGE("write: start write back file buffer");
            int write_into_buffer_size = MAX_GROUP_SIZE - in_buffer_data->byte_cnt;
            memcpy(in_buffer_data->buf + in_buffer_data->byte_cnt, curr_buf_ptr, write_into_buffer_size);
            less_size -= write_into_buffer_size;
            curr_buf_ptr += write_into_buffer_size;
            // content defined chunking not implement -_-!
            int cut_pos = MAX_GROUP_SIZE; // use fixed chunking for now
            // hashing
            char cur_fp[SHA_DIGEST_LENGTH];
            SHA1((const unsigned char *)in_buffer_data->buf, cut_pos, (unsigned char *)cur_fp);
            FP_TYPE new_fp(cur_fp, SHA_DIGEST_LENGTH);
            // query fp store
            auto fp_store_iter = fp_store.find(new_fp);
            if (fp_store_iter != fp_store.end()){   // found
                DEBUG_MESSAGE("write: found duplicate group!!");
                mapping_table[iNum].group_pos.push_back(fp_store_iter->second);
                mapping_table[iNum].group_offset.push_back(file_buffer[iNum].st_byte);
                for(int i = 0; i < cut_pos / BLOCK_SIZE; i++){
                    mapping_table[iNum].group_id.push_back(mapping_table[iNum].group_pos.size() - 1);
                }
            }
            else{                                   // not found
                group_addr *new_group_addr = new group_addr;
                new_group_addr->iNum = iNum;
                new_group_addr->file_st_blk = mapping_table[iNum].actual_size_in_disk / BLOCK_SIZE;
                new_group_addr->file_blk_cnt = cut_pos / BLOCK_SIZE;
                int res = pwrite(fi->fh, in_buffer_data->buf, cut_pos, mapping_table[iNum].actual_size_in_disk);
                if (res == -1){
                    PRINT_WARNING("write: write back to disk failed!!");
                    delete new_group_addr;
                    return -errno;
                }
                mapping_table[iNum].actual_size_in_disk += cut_pos;
                mapping_table[iNum].group_pos.push_back(new_group_addr);
                mapping_table[iNum].group_offset.push_back(file_buffer[iNum].st_byte);
                for(int i = 0; i < cut_pos / BLOCK_SIZE; i++){
                    mapping_table[iNum].group_id.push_back(mapping_table[iNum].group_pos.size() - 1);
                }
                fp_store[new_fp] = new_group_addr;
            }
            // update buffer
            if (cut_pos < MAX_GROUP_SIZE){
                memcpy(in_buffer_data->buf, in_buffer_data->buf + cut_pos, MAX_GROUP_SIZE - cut_pos);
            }
            in_buffer_data->st_byte += cut_pos;
            in_buffer_data->byte_cnt = MAX_GROUP_SIZE - cut_pos;
        }
        else{
            memcpy(in_buffer_data->buf + in_buffer_data->byte_cnt, buf, less_size);
            in_buffer_data->byte_cnt += less_size;
            less_size = 0;
        }
    }
    
    return size;
}

static int cdcfs_truncate(const char *path, off_t size) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("truncate: " << path << " to " << size);

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

/*static int cdcfs_unlink(const char *path) {
    int res;
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", BACKEND, path);
    DEBUG_MESSAGE("unlink: " << path);
    
    res = unlink(full_path);
    if (res == -1) {
        return -errno;
    }
    return 0;
}*/

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