#include "vfs.h"
#include "log.h"

#include <dirent.h>
#include <errno.h>
#include <orbis/libkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static OrbisPthread g_worker_thread = 0;

struct WorkerJob {
    FileOpType type;
    std::vector<std::string> sources;
    std::string dest_dir;
    FileOpProgress* progress;
};

static const char* get_ext(const char* name) {
    if (!name) return "";
    const char* dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

static FileCategory classify_file(const char* name, bool is_dir, bool is_symlink) {
    if (is_dir) return FileCategory::Folder;
    if (is_symlink) return FileCategory::Symlink;

    const char* ext = get_ext(name);
    if (strcasecmp(ext, "pkg") == 0) return FileCategory::Pkg;
    if (strcasecmp(ext, "elf") == 0 || strcasecmp(ext, "bin") == 0 || strcasecmp(ext, "self") == 0 || strcasecmp(ext, "oelf") == 0)
        return FileCategory::Executable;
    if (strcasecmp(ext, "prx") == 0 || strcasecmp(ext, "sprx") == 0 || strcasecmp(ext, "so") == 0)
        return FileCategory::Plugin;
    if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0 ||
        strcasecmp(ext, "bmp") == 0 || strcasecmp(ext, "dds") == 0 || strcasecmp(ext, "gif") == 0)
        return FileCategory::Image;
    if (strcasecmp(ext, "mp4") == 0 || strcasecmp(ext, "mkv") == 0 || strcasecmp(ext, "avi") == 0 ||
        strcasecmp(ext, "webm") == 0 || strcasecmp(ext, "mov") == 0)
        return FileCategory::Video;
    if (strcasecmp(ext, "mp3") == 0 || strcasecmp(ext, "wav") == 0 || strcasecmp(ext, "ogg") == 0 ||
        strcasecmp(ext, "flac") == 0 || strcasecmp(ext, "at9") == 0)
        return FileCategory::Audio;
    if (strcasecmp(ext, "zip") == 0 || strcasecmp(ext, "tar") == 0 || strcasecmp(ext, "gz") == 0 ||
        strcasecmp(ext, "7z") == 0 || strcasecmp(ext, "rar") == 0)
        return FileCategory::Archive;
    if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "log") == 0 || strcasecmp(ext, "cfg") == 0 ||
        strcasecmp(ext, "conf") == 0 || strcasecmp(ext, "ini") == 0 || strcasecmp(ext, "json") == 0 ||
        strcasecmp(ext, "xml") == 0 || strcasecmp(ext, "md") == 0 || strcasecmp(ext, "gp4") == 0 ||
        strcasecmp(ext, "c") == 0 || strcasecmp(ext, "cpp") == 0 || strcasecmp(ext, "h") == 0)
        return FileCategory::Text;

    return FileCategory::Other;
}

const char* vfs_category_name(FileCategory cat) {
    switch (cat) {
        case FileCategory::Folder: return "Folder";
        case FileCategory::Pkg: return "PS4 Package";
        case FileCategory::Executable: return "Executable";
        case FileCategory::Plugin: return "PRX Module";
        case FileCategory::Image: return "Image";
        case FileCategory::Video: return "Video";
        case FileCategory::Audio: return "Audio";
        case FileCategory::Archive: return "Archive";
        case FileCategory::Text: return "Text/Config";
        case FileCategory::Symlink: return "Symlink";
        default: return "File";
    }
}

void vfs_format_size(uint64_t bytes, char* buf, size_t len) {
    if (bytes < 1024ULL) {
        snprintf(buf, len, "%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024ULL * 1024ULL) {
        snprintf(buf, len, "%.1f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        snprintf(buf, len, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, len, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

void vfs_format_mode(uint32_t mode, bool is_dir, bool is_link, char* buf, size_t len) {
    if (len < 11) return;
    buf[0] = is_dir ? 'd' : (is_link ? 'l' : '-');
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';
    buf[4] = (mode & 0040) ? 'r' : '-';
    buf[5] = (mode & 0020) ? 'w' : '-';
    buf[6] = (mode & 0010) ? 'x' : '-';
    buf[7] = (mode & 0004) ? 'r' : '-';
    buf[8] = (mode & 0002) ? 'w' : '-';
    buf[9] = (mode & 0001) ? 'x' : '-';
    buf[10] = '\0';
}

void vfs_parent_path(const char* path, char* out_parent, size_t len) {
    if (!path || !*path || strcmp(path, "/") == 0) {
        snprintf(out_parent, len, "/");
        return;
    }
    snprintf(out_parent, len, "%s", path);
    size_t slen = strlen(out_parent);
    while (slen > 1 && out_parent[slen - 1] == '/') {
        out_parent[--slen] = '\0';
    }
    char* slash = strrchr(out_parent, '/');
    if (!slash || slash == out_parent) {
        snprintf(out_parent, len, "/");
    } else {
        *slash = '\0';
    }
}

void vfs_join_path(const char* dir, const char* name, char* out_path, size_t len) {
    if (!dir || !*dir || strcmp(dir, "/") == 0) {
        snprintf(out_path, len, "/%s", name ? name : "");
    } else {
        size_t dlen = strlen(dir);
        if (dir[dlen - 1] == '/') {
            snprintf(out_path, len, "%s%s", dir, name ? name : "");
        } else {
            snprintf(out_path, len, "%s/%s", dir, name ? name : "");
        }
    }
}

static SortColumn g_sort_col = SortColumn::Name;
static bool g_sort_asc = true;

static int compare_entries(const DirEntry& a, const DirEntry& b) {
    // Directories always first (classic PCManFM behavior)
    if (a.is_dir != b.is_dir) {
        return a.is_dir ? -1 : 1;
    }
    int cmp = 0;
    switch (g_sort_col) {
        case SortColumn::Size:
            if (a.size < b.size) cmp = -1;
            else if (a.size > b.size) cmp = 1;
            else cmp = strcasecmp(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Type:
            cmp = strcasecmp(vfs_category_name(a.category), vfs_category_name(b.category));
            if (cmp == 0) cmp = strcasecmp(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Modified:
            if (a.mtime < b.mtime) cmp = -1;
            else if (a.mtime > b.mtime) cmp = 1;
            else cmp = strcasecmp(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Name:
        default:
            cmp = strcasecmp(a.name.c_str(), b.name.c_str());
            break;
    }
    return g_sort_asc ? cmp : -cmp;
}

static void sort_entries_quicksort(std::vector<DirEntry>& arr, int left, int right) {
    if (left >= right) return;
    int i = left;
    int j = right;
    DirEntry pivot = arr[(left + right) / 2];
    while (i <= j) {
        while (compare_entries(arr[i], pivot) < 0) i++;
        while (compare_entries(arr[j], pivot) > 0) j--;
        if (i <= j) {
            DirEntry tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
            i++;
            j--;
        }
    }
    if (left < j) sort_entries_quicksort(arr, left, j);
    if (i < right) sort_entries_quicksort(arr, i, right);
}

bool vfs_scan_directory(const char* path,
                        bool show_hidden,
                        SortColumn sort_col,
                        bool sort_asc,
                        std::vector<DirEntry>& out_entries,
                        std::string& out_error) {
    out_entries.clear();
    out_error.clear();

    DIR* dir = opendir(path);
    if (!dir) {
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "opendir(\"%s\") failed: %s (errno=%d)", path, strerror(errno), errno);
        out_error = err_buf;
        log_msg("VFS", "%s", err_buf);
        return false;
    }

    char* full_buf = new char[2048];
    struct dirent* ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        bool is_hidden = (ent->d_name[0] == '.');
        if (!show_hidden && is_hidden) {
            continue;
        }

        vfs_join_path(path, ent->d_name, full_buf, 2048);

        struct stat st;
        memset(&st, 0, sizeof(st));
        bool have_stat = (lstat(full_buf, &st) == 0);

        bool is_symlink = false;
        bool is_dir = false;

        if (have_stat) {
            is_symlink = S_ISLNK(st.st_mode);
            is_dir = S_ISDIR(st.st_mode);
            if (is_symlink) {
                struct stat target_st;
                if (stat(full_buf, &target_st) == 0 && S_ISDIR(target_st.st_mode)) {
                    is_dir = true;
                }
            }
        } else {
            // Handle DT_UNKNOWN and fallback types cleanly
            if (ent->d_type == DT_DIR) {
                is_dir = true;
            } else if (ent->d_type == DT_LNK) {
                is_symlink = true;
            } else if (ent->d_type == DT_UNKNOWN) {
                DIR* probe = opendir(full_buf);
                if (probe) {
                    is_dir = true;
                    closedir(probe);
                }
            }
        }

        DirEntry item;
        item.name = ent->d_name;
        item.full_path = full_buf;
        item.size = have_stat ? (uint64_t)st.st_size : 0ULL;
        item.mode = have_stat ? (uint32_t)(st.st_mode & 07777) : 0;
        item.mtime = have_stat ? (int64_t)st.st_mtime : 0;
        item.is_dir = is_dir;
        item.is_symlink = is_symlink;
        item.is_hidden = is_hidden;
        item.category = classify_file(ent->d_name, is_dir, is_symlink);
        out_entries.push_back(item);
    }

    delete[] full_buf;
    closedir(dir);

    g_sort_col = sort_col;
    g_sort_asc = sort_asc;
    if (!out_entries.empty()) {
        sort_entries_quicksort(out_entries, 0, (int)out_entries.size() - 1);
    }

    log_msg("VFS", "Scanned \"%s\" -> %zu entries", path, out_entries.size());
    return true;
}

bool vfs_get_disk_space(const char* path, uint64_t& out_total, uint64_t& out_free) {
    (void)path;
    out_total = 0;
    out_free = 0;
    return false;
}

bool vfs_mkdir(const char* path, std::string& out_error) {
    if (mkdir(path, 0777) == 0) {
        log_msg("VFS", "mkdir(\"%s\") OK", path);
        return true;
    }
    char err[256];
    snprintf(err, sizeof(err), "mkdir failed: %s", strerror(errno));
    out_error = err;
    log_msg("VFS", "mkdir(\"%s\") FAILED: %s", path, strerror(errno));
    return false;
}

bool vfs_rename(const char* old_path, const char* new_path, std::string& out_error) {
    if (rename(old_path, new_path) == 0) {
        log_msg("VFS", "rename(\"%s\" -> \"%s\") OK", old_path, new_path);
        return true;
    }
    char err[256];
    snprintf(err, sizeof(err), "rename failed: %s", strerror(errno));
    out_error = err;
    log_msg("VFS", "rename(\"%s\" -> \"%s\") FAILED: %s", old_path, new_path, strerror(errno));
    return false;
}

bool vfs_chmod(const char* path, uint32_t mode, std::string& out_error) {
    if (chmod(path, (mode_t)mode) == 0) {
        log_msg("VFS", "chmod(\"%s\", 0%o) OK", path, mode);
        return true;
    }
    char err[256];
    snprintf(err, sizeof(err), "chmod failed: %s", strerror(errno));
    out_error = err;
    log_msg("VFS", "chmod(\"%s\", 0%o) FAILED: %s", path, mode, strerror(errno));
    return false;
}

bool vfs_read_file_preview(const char* path,
                           size_t max_bytes,
                           std::vector<uint8_t>& out_data,
                           uint64_t& out_total_size,
                           std::string& out_error) {
    out_data.clear();
    out_total_size = 0;
    out_error.clear();

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        char err[256];
        snprintf(err, sizeof(err), "fopen(\"%s\") failed: %s", path, strerror(errno));
        out_error = err;
        log_msg("VFS", "%s", err);
        return false;
    }

    // Use fseeko/ftello instead of stat().st_size per Orbis OS best practices
    if (fseeko(fp, 0, SEEK_END) == 0) {
        off_t sz = ftello(fp);
        if (sz > 0) out_total_size = (uint64_t)sz;
        fseeko(fp, 0, SEEK_SET);
    }

    size_t to_read = max_bytes;
    if (out_total_size > 0 && out_total_size < (uint64_t)to_read) {
        to_read = (size_t)out_total_size;
    }

    if (to_read > 0) {
        out_data.resize(to_read);
        size_t got = fread(out_data.data(), 1, to_read, fp);
        out_data.resize(got);
        if (out_total_size == 0) {
            out_total_size = (uint64_t)got;
        }
    }
    fclose(fp);
    return true;
}

static bool copy_single_file(const char* src, const char* dst, FileOpProgress* prog, uint8_t* io_buf, size_t io_buf_sz) {
    FILE* in = fopen(src, "rb");
    if (!in) {
        snprintf(prog->error_msg, sizeof(prog->error_msg), "Cannot open source: %s (%s)", src, strerror(errno));
        return false;
    }
    FILE* out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        snprintf(prog->error_msg, sizeof(prog->error_msg), "Cannot create dest: %s (%s)", dst, strerror(errno));
        return false;
    }

    bool ok = true;
    while (!__atomic_load_n(&prog->cancel_req, __ATOMIC_ACQUIRE)) {
        size_t n = fread(io_buf, 1, io_buf_sz, in);
        if (n > 0) {
            size_t w = fwrite(io_buf, 1, n, out);
            if (w != n) {
                snprintf(prog->error_msg, sizeof(prog->error_msg), "Write failed: %s (%s)", dst, strerror(errno));
                ok = false;
                break;
            }
            __atomic_add_fetch(&prog->bytes_done, (uint64_t)w, __ATOMIC_RELAXED);
        }
        if (n < io_buf_sz) {
            if (ferror(in)) {
                snprintf(prog->error_msg, sizeof(prog->error_msg), "Read failed: %s (%s)", src, strerror(errno));
                ok = false;
            }
            break;
        }
    }

    fclose(out);
    fclose(in);
    return ok;
}

static bool copy_recursive(const char* src, const char* dst, FileOpProgress* prog, uint8_t* io_buf, size_t io_buf_sz) {
    if (__atomic_load_n(&prog->cancel_req, __ATOMIC_ACQUIRE)) return false;

    snprintf(prog->current_file, sizeof(prog->current_file), "%s", src);

    struct stat st;
    if (lstat(src, &st) != 0) {
        snprintf(prog->error_msg, sizeof(prog->error_msg), "stat failed: %s (%s)", src, strerror(errno));
        return false;
    }

    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, 0777);
        DIR* dir = opendir(src);
        if (!dir) {
            snprintf(prog->error_msg, sizeof(prog->error_msg), "opendir failed: %s (%s)", src, strerror(errno));
            return false;
        }
        char* next_src = new char[2048];
        char* next_dst = new char[2048];
        bool ok = true;
        struct dirent* ent = NULL;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            vfs_join_path(src, ent->d_name, next_src, 2048);
            vfs_join_path(dst, ent->d_name, next_dst, 2048);
            if (!copy_recursive(next_src, next_dst, prog, io_buf, io_buf_sz)) {
                ok = false;
                break;
            }
        }
        delete[] next_src;
        delete[] next_dst;
        closedir(dir);
        __atomic_add_fetch(&prog->items_done, 1ULL, __ATOMIC_RELAXED);
        return ok;
    }

    bool ok = copy_single_file(src, dst, prog, io_buf, io_buf_sz);
    __atomic_add_fetch(&prog->items_done, 1ULL, __ATOMIC_RELAXED);
    return ok;
}

static bool delete_recursive(const char* target, FileOpProgress* prog) {
    if (__atomic_load_n(&prog->cancel_req, __ATOMIC_ACQUIRE)) return false;
    snprintf(prog->current_file, sizeof(prog->current_file), "%s", target);

    struct stat st;
    if (lstat(target, &st) != 0) {
        snprintf(prog->error_msg, sizeof(prog->error_msg), "stat failed: %s (%s)", target, strerror(errno));
        return false;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(target);
        if (!dir) {
            snprintf(prog->error_msg, sizeof(prog->error_msg), "opendir failed: %s (%s)", target, strerror(errno));
            return false;
        }
        char* child = new char[2048];
        bool ok = true;
        struct dirent* ent = NULL;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            vfs_join_path(target, ent->d_name, child, 2048);
            if (!delete_recursive(child, prog)) {
                ok = false;
                break;
            }
        }
        delete[] child;
        closedir(dir);
        if (ok && rmdir(target) != 0) {
            snprintf(prog->error_msg, sizeof(prog->error_msg), "rmdir failed: %s (%s)", target, strerror(errno));
            ok = false;
        }
        __atomic_add_fetch(&prog->items_done, 1ULL, __ATOMIC_RELAXED);
        return ok;
    }

    if (unlink(target) != 0) {
        snprintf(prog->error_msg, sizeof(prog->error_msg), "unlink failed: %s (%s)", target, strerror(errno));
        return false;
    }
    __atomic_add_fetch(&prog->items_done, 1ULL, __ATOMIC_RELAXED);
    return true;
}

static void* file_op_worker(void* arg) {
    WorkerJob* job = (WorkerJob*)arg;
    FileOpProgress* prog = job->progress;
    const size_t io_buf_sz = 65536;
    uint8_t* io_buf = new uint8_t[io_buf_sz];
    bool all_ok = true;

    log_msg("VFS_OP", "Worker started: op=%d count=%zu dest=\"%s\"",
            (int)job->type, job->sources.size(), job->dest_dir.c_str());

    for (size_t i = 0; i < job->sources.size(); ++i) {
        if (__atomic_load_n(&prog->cancel_req, __ATOMIC_ACQUIRE)) {
            all_ok = false;
            break;
        }
        const std::string& src = job->sources[i];
        const char* base = strrchr(src.c_str(), '/');
        base = base ? base + 1 : src.c_str();

        if (job->type == FileOpType::Delete) {
            if (!delete_recursive(src.c_str(), prog)) {
                all_ok = false;
                break;
            }
        } else {
            char* dst_path = new char[2048];
            vfs_join_path(job->dest_dir.c_str(), base, dst_path, 2048);

            if (strcmp(src.c_str(), dst_path) == 0) {
                snprintf(prog->error_msg, sizeof(prog->error_msg), "Source and destination are identical: %s", src.c_str());
                delete[] dst_path;
                all_ok = false;
                break;
            }

            if (job->type == FileOpType::Move) {
                if (rename(src.c_str(), dst_path) == 0) {
                    __atomic_add_fetch(&prog->items_done, 1ULL, __ATOMIC_RELAXED);
                } else {
                    // Cross-device move: copy then delete
                    if (!copy_recursive(src.c_str(), dst_path, prog, io_buf, io_buf_sz) ||
                        !delete_recursive(src.c_str(), prog)) {
                        delete[] dst_path;
                        all_ok = false;
                        break;
                    }
                }
            } else if (job->type == FileOpType::Copy) {
                if (!copy_recursive(src.c_str(), dst_path, prog, io_buf, io_buf_sz)) {
                    delete[] dst_path;
                    all_ok = false;
                    break;
                }
            }
            delete[] dst_path;
        }
    }

    delete[] io_buf;
    __atomic_store_n(&prog->ok, all_ok ? 1 : 0, __ATOMIC_RELEASE);
    __atomic_store_n(&prog->active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&prog->done, 1, __ATOMIC_RELEASE);

    log_msg("VFS_OP", "Worker finished: ok=%d err=\"%s\"", all_ok ? 1 : 0, prog->error_msg);
    delete job;
    return NULL;
}

bool vfs_start_file_op(FileOpType type,
                       const std::vector<std::string>& sources,
                       const char* dest_dir,
                       FileOpProgress* progress) {
    if (!progress || sources.empty()) return false;
    if (__atomic_load_n(&progress->active, __ATOMIC_ACQUIRE)) return false;

    memset(progress, 0, sizeof(*progress));
    progress->type = type;
    progress->items_total = (uint64_t)sources.size();
    __atomic_store_n(&progress->active, 1, __ATOMIC_RELEASE);

    WorkerJob* job = new WorkerJob();
    job->type = type;
    job->sources = sources;
    job->dest_dir = dest_dir ? dest_dir : "";
    job->progress = progress;

    int32_t rc = scePthreadCreate(&g_worker_thread, NULL, file_op_worker, job, "pcmanfm_worker");
    log_msg("VFS_OP", "scePthreadCreate() -> 0x%08x", rc);
    if (rc != 0) {
        __atomic_store_n(&progress->active, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&progress->done, 1, __ATOMIC_RELEASE);
        snprintf(progress->error_msg, sizeof(progress->error_msg), "scePthreadCreate failed: 0x%08x", rc);
        delete job;
        return false;
    }
    return true;
}

void vfs_poll_file_op_join(FileOpProgress* progress) {
    if (!progress) return;
    if (__atomic_load_n(&progress->done, __ATOMIC_ACQUIRE) && g_worker_thread != 0) {
        scePthreadJoin(g_worker_thread, NULL);
        g_worker_thread = 0;
    }
}

void vfs_load_bookmarks(std::vector<BookmarkItem>& out_bookmarks) {
    out_bookmarks.clear();
    FILE* fp = fopen("/data/pcmanfm/bookmarks.txt", "r");
    if (!fp) return;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        char* sep = strchr(line, '|');
        BookmarkItem bm;
        if (sep) {
            *sep = '\0';
            bm.label = line;
            bm.path = sep + 1;
        } else {
            const char* base = strrchr(line, '/');
            bm.label = (base && base[1]) ? (base + 1) : line;
            bm.path = line;
        }
        out_bookmarks.push_back(bm);
    }
    fclose(fp);
}

void vfs_save_bookmarks(const std::vector<BookmarkItem>& bookmarks) {
    FILE* fp = fopen("/data/pcmanfm/bookmarks.txt", "w");
    if (!fp) return;
    for (size_t i = 0; i < bookmarks.size(); ++i) {
        fprintf(fp, "%s|%s\n", bookmarks[i].label.c_str(), bookmarks[i].path.c_str());
    }
    fflush(fp);
    fclose(fp);
}
