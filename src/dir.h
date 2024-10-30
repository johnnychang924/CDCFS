#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <map>
#include <vector>
#include <stack>
#include <shared_mutex>
#include <sstream>
#include "def.h"

struct node{
    node(bool is_directory): is_directory(is_directory) {
        if (is_directory){
            new (&sub_dir) std::map<std::string, node*>();
        }
    }
    ~node() {
        sub_dir.~map(); // Call the destructor for the map
    }
    bool is_directory;
    std::shared_mutex file_lock;            // for multi-thread access
    union {
        std::map<std::string,node*> sub_dir;   // if is directory
        INUM_TYPE iNum;                         // if is a file
    };
};

static node *root = new node(true);

inline std::vector<std::string> split_path(PATH_TYPE path_str) {
    std::vector<std::string> parts;
    std::stringstream ss(path_str);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return parts;
}

/*
* can get either file node and directory node
*/
inline node* get_node(std::vector<std::string> splited_path){
    node *target = root;
    std::stack<node*> history;
    for (const auto &path_part : splited_path) {
        if (path_part == "..") {
            target = history.top();
            history.pop();
            continue;
        }
        else if (path_part == ".") continue;
        else {
            auto it = target->sub_dir.find(path_part);
            if (it == target->sub_dir.end()) return NULL;
            else target = it->second;
        }
        history.push(target);
    }
    return target;
}

static int cdcfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    
    DEBUG_MESSAGE("[read dir]" << path);
    node *target = get_node(split_path((std::string)path));
    if (target == NULL || !target->is_directory){
        DEBUG_MESSAGE("  failed");
        return -errno;
    }
    for (auto &[ent_name, ent_node] : target->sub_dir) {
        DEBUG_MESSAGE("fill: " << ent_name);
        filler(buf, ent_name.c_str(), NULL, 0);      // not implement stat return
    }
    return 0;
}

static int cdcfs_mkdir(const char *path, mode_t mode) {
    DEBUG_MESSAGE("[create dir]" << path);
    std::vector<std::string> parent_splited_path = split_path((std::string)path);
    if (parent_splited_path.size() == 0) return -errno;
    std::string new_dir_name = parent_splited_path.back();
    parent_splited_path.pop_back();
    node *parent = get_node(parent_splited_path);
    if  (parent == NULL){
        DEBUG_MESSAGE("  can not find parent directory");
        return -errno;
    }
    else if (!parent->is_directory){
        DEBUG_MESSAGE("  parent is not a directory");
        return -errno;
    }
    else if (parent->sub_dir.find(new_dir_name) != parent->sub_dir.end()) DEBUG_MESSAGE(" directory already exist");
    parent->sub_dir[new_dir_name] = new node(true);
    return 0;
}

static int cdcfs_rmdir(const char *path) {
    DEBUG_MESSAGE("[remove dir]" << path);
    // get parent and target dir
    std::vector<std::string> splited_path = split_path((std::string)path);
    if (splited_path.size() == 0) return -errno;    // try to remove root? good idea
    std::string victim_dir_name = splited_path.back();
    node *target = get_node(splited_path);
    splited_path.pop_back();
    node *parent = get_node(splited_path);
    // check operation is valid
    if (parent == NULL || target == NULL){
        DEBUG_MESSAGE("  can not find directory");
        return -errno;
    }
    if (!target->is_directory) {
        DEBUG_MESSAGE("  target is not a directory");
        return -errno;
    }
    else if (target->sub_dir.size() > 0){
        DEBUG_MESSAGE("  target has sub directories or files");
        return -errno;
    }
    // remove target dir from parent's sub dirs list
    parent->sub_dir.erase(victim_dir_name);
    delete target;
    return 0;
}