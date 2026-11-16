#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <string>
#include <vector>

enum class FileCategory {
    Folder = 0,
    Pkg,
    Executable,
    Plugin,
    Image,
    Video,
    Audio,
    Archive,
    Text,
    Symlink,
    Other
};

enum class SortColumn {
    Name = 0,
    Size,
    Type,
    Modified
};

struct DirEntry {
    std::string name;
    std::string full_path;
    uint64_t size;
    uint32_t mode;
    int64_t mtime;
    bool is_dir;
    bool is_symlink;
    bool is_hidden;
    FileCategory category;
};

struct BookmarkItem {
    std::string label;
    std::string path;
};

enum class FileOpType {
    None = 0,
    Copy,
    Move,
    Delete
};

struct FileOpProgress {
    int active;        // atomic 0/1
    int done;          // atomic 0/1
    int cancel_req;    // atomic 0/1
    int ok;            // atomic 0/1
    FileOpType type;
    uint64_t items_done;
    uint64_t items_total;
    uint64_t bytes_done;
    uint64_t bytes_total;
    char current_file[512];
    char error_msg[512];
};

bool vfs_scan_directory(const char* path,
                        bool show_hidden,
                        SortColumn sort_col,
                        bool sort_asc,
                        std::vector<DirEntry>& out_entries,
                        std::string& out_error);

bool vfs_get_disk_space(const char* path, uint64_t& out_total, uint64_t& out_free);
void vfs_format_size(uint64_t bytes, char* buf, size_t len);
const char* vfs_category_name(FileCategory cat);
void vfs_format_mode(uint32_t mode, bool is_dir, bool is_link, char* buf, size_t len);
void vfs_parent_path(const char* path, char* out_parent, size_t len);
void vfs_join_path(const char* dir, const char* name, char* out_path, size_t len);

bool vfs_mkdir(const char* path, std::string& out_error);
bool vfs_rename(const char* old_path, const char* new_path, std::string& out_error);
bool vfs_chmod(const char* path, uint32_t mode, std::string& out_error);

bool vfs_read_file_preview(const char* path,
                           size_t max_bytes,
                           std::vector<uint8_t>& out_data,
                           uint64_t& out_total_size,
                           std::string& out_error);

// Background threaded copy/move/delete
bool vfs_start_file_op(FileOpType type,
                       const std::vector<std::string>& sources,
                       const char* dest_dir,
                       FileOpProgress* progress);
void vfs_poll_file_op_join(FileOpProgress* progress);

// Bookmarks (/data/pcmanfm/bookmarks.txt)
void vfs_load_bookmarks(std::vector<BookmarkItem>& out_bookmarks);
void vfs_save_bookmarks(const std::vector<BookmarkItem>& bookmarks);
