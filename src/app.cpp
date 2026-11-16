#include "app.h"
#include "imgui.h"
#include "log.h"
#include "ps4_ime.h"

#include <ctype.h>
#include <orbis/libkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

static bool str_icontains(const char* haystack, const char* needle) {
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    size_t nlen = strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

static ImVec4 category_color(FileCategory cat, bool dark_theme) {
    switch (cat) {
        case FileCategory::Folder:     return dark_theme ? ImVec4(0.96f, 0.78f, 0.28f, 1.0f) : ImVec4(0.80f, 0.52f, 0.05f, 1.0f);
        case FileCategory::Pkg:        return dark_theme ? ImVec4(0.35f, 0.78f, 0.98f, 1.0f) : ImVec4(0.05f, 0.48f, 0.78f, 1.0f);
        case FileCategory::Executable: return dark_theme ? ImVec4(0.42f, 0.90f, 0.50f, 1.0f) : ImVec4(0.10f, 0.58f, 0.20f, 1.0f);
        case FileCategory::Plugin:     return dark_theme ? ImVec4(0.75f, 0.58f, 0.98f, 1.0f) : ImVec4(0.45f, 0.22f, 0.72f, 1.0f);
        case FileCategory::Image:      return dark_theme ? ImVec4(0.95f, 0.55f, 0.72f, 1.0f) : ImVec4(0.72f, 0.20f, 0.45f, 1.0f);
        case FileCategory::Video:      return dark_theme ? ImVec4(0.95f, 0.52f, 0.35f, 1.0f) : ImVec4(0.76f, 0.28f, 0.12f, 1.0f);
        case FileCategory::Audio:      return dark_theme ? ImVec4(0.45f, 0.88f, 0.82f, 1.0f) : ImVec4(0.12f, 0.55f, 0.50f, 1.0f);
        case FileCategory::Archive:    return dark_theme ? ImVec4(0.92f, 0.45f, 0.45f, 1.0f) : ImVec4(0.72f, 0.16f, 0.16f, 1.0f);
        case FileCategory::Text:       return dark_theme ? ImVec4(0.86f, 0.88f, 0.92f, 1.0f) : ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
        case FileCategory::Symlink:    return dark_theme ? ImVec4(0.55f, 0.88f, 0.98f, 1.0f) : ImVec4(0.08f, 0.52f, 0.68f, 1.0f);
        default:                       return dark_theme ? ImVec4(0.74f, 0.76f, 0.80f, 1.0f) : ImVec4(0.28f, 0.30f, 0.34f, 1.0f);
    }
}

static const char* category_badge(FileCategory cat) {
    switch (cat) {
        case FileCategory::Folder:     return "[DIR]";
        case FileCategory::Pkg:        return "[PKG]";
        case FileCategory::Executable: return "[ELF]";
        case FileCategory::Plugin:     return "[PRX]";
        case FileCategory::Image:      return "[IMG]";
        case FileCategory::Video:      return "[VID]";
        case FileCategory::Audio:      return "[AUD]";
        case FileCategory::Archive:    return "[ARC]";
        case FileCategory::Text:       return "[TXT]";
        case FileCategory::Symlink:    return "[LNK]";
        default:                       return "[---]";
    }
}

PcmanfmApp::PcmanfmApp()
    : m_active_pane(0),
      m_clipboard_is_cut(false),
      m_show_op_modal(false),
      m_show_actions_menu(false),
      m_show_delete_confirm(false),
      m_show_properties(false),
      m_show_text_viewer(false),
      m_show_hex_viewer(false),
      m_show_prefs(false),
      m_show_help(false),
      m_preview_total_size(0),
      m_toast_until_us(0),
      m_ime_purpose(ImePurpose::None),
      m_ime_using_ps4(false),
      m_ime_fallback_open(false),
      m_tree_root("/data"),
      m_exit_requested(false) {
    memset(&m_op_prog, 0, sizeof(m_op_prog));
    memset(m_ime_title, 0, sizeof(m_ime_title));
    memset(m_ime_buf, 0, sizeof(m_ime_buf));

    m_cfg.show_hidden = false;
    m_cfg.dual_pane = false;
    m_cfg.confirm_delete = true;
    m_cfg.dark_theme = true;
    m_cfg.view_mode = ViewMode::Detailed;
    m_cfg.sort_col = SortColumn::Name;
    m_cfg.sort_asc = true;
    m_cfg.ui_scale = 1.65f;
}

PcmanfmApp::~PcmanfmApp() {
}

void PcmanfmApp::load_config(void) {
    FILE* fp = fopen("/data/pcmanfm/pcmanfm.conf", "r");
    if (!fp) {
        log_msg("CFG", "No existing config, using defaults");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if (strcmp(key, "show_hidden") == 0) m_cfg.show_hidden = (atoi(val) != 0);
            else if (strcmp(key, "dual_pane") == 0) m_cfg.dual_pane = (atoi(val) != 0);
            else if (strcmp(key, "confirm_delete") == 0) m_cfg.confirm_delete = (atoi(val) != 0);
            else if (strcmp(key, "dark_theme") == 0) m_cfg.dark_theme = (atoi(val) != 0);
            else if (strcmp(key, "view_mode") == 0) m_cfg.view_mode = (ViewMode)atoi(val);
            else if (strcmp(key, "ui_scale") == 0) {
                float s = (float)atof(val);
                if (s >= 1.0f && s <= 2.5f) m_cfg.ui_scale = s;
            }
        }
    }
    fclose(fp);
    log_msg("CFG", "Loaded config from /data/pcmanfm/pcmanfm.conf");
}

void PcmanfmApp::save_config(void) {
    FILE* fp = fopen("/data/pcmanfm/pcmanfm.conf", "w");
    if (!fp) return;
    fprintf(fp, "show_hidden=%d\n", m_cfg.show_hidden ? 1 : 0);
    fprintf(fp, "dual_pane=%d\n", m_cfg.dual_pane ? 1 : 0);
    fprintf(fp, "confirm_delete=%d\n", m_cfg.confirm_delete ? 1 : 0);
    fprintf(fp, "dark_theme=%d\n", m_cfg.dark_theme ? 1 : 0);
    fprintf(fp, "view_mode=%d\n", (int)m_cfg.view_mode);
    fprintf(fp, "ui_scale=%.2f\n", m_cfg.ui_scale);
    fflush(fp);
    fclose(fp);
    log_msg("CFG", "Saved config");
}

void PcmanfmApp::apply_theme(void) {
    if (m_cfg.dark_theme) {
        ImGui::StyleColorsDark();
        ImGuiStyle& st = ImGui::GetStyle();
        st.WindowRounding = 0.0f;
        st.FrameRounding = 3.0f;
        st.ItemSpacing = ImVec2(8, 6);
        st.FramePadding = ImVec2(8, 5);
        st.Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.15f, 1.0f);
        st.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.42f, 0.68f, 0.70f);
        st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.50f, 0.80f, 0.85f);
        st.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.24f, 0.30f, 1.0f);
        st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.44f, 0.66f, 1.0f);
    } else {
        ImGui::StyleColorsLight();
        ImGuiStyle& st = ImGui::GetStyle();
        st.WindowRounding = 0.0f;
        st.FrameRounding = 3.0f;
        st.ItemSpacing = ImVec2(8, 6);
        st.FramePadding = ImVec2(8, 5);
    }
    ImGui::GetIO().FontGlobalScale = m_cfg.ui_scale;
}

bool PcmanfmApp::init(void) {
    log_msg("APP", "PcmanfmApp::init()");
    load_config();
    apply_theme();

    vfs_load_bookmarks(m_bookmarks);
    if (m_bookmarks.empty()) {
        m_bookmarks.push_back({"Data (/data)", "/data"});
        m_bookmarks.push_back({"PKG Staging", "/data/pkg"});
        m_bookmarks.push_back({"USB 0", "/mnt/usb0"});
        vfs_save_bookmarks(m_bookmarks);
    }

    m_panes[0].active_tab = 0;
    m_panes[1].active_tab = 0;
    open_new_tab(0, "/data");
    open_new_tab(1, "/mnt/usb0");

    set_toast("PCManFM-PS4 ready | Press Triangle for Actions");
    return true;
}

void PcmanfmApp::shutdown(void) {
    log_msg("APP", "PcmanfmApp::shutdown()");
    save_config();
    vfs_save_bookmarks(m_bookmarks);
    if (__atomic_load_n(&m_op_prog.active, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&m_op_prog.cancel_req, 1, __ATOMIC_RELEASE);
    }
    vfs_poll_file_op_join(&m_op_prog);
}

TabState& PcmanfmApp::cur_tab(int pane_idx) {
    PaneState& p = m_panes[pane_idx];
    if (p.tabs.empty()) {
        open_new_tab(pane_idx, "/data");
    }
    if (p.active_tab < 0) p.active_tab = 0;
    if (p.active_tab >= (int)p.tabs.size()) p.active_tab = (int)p.tabs.size() - 1;
    return p.tabs[p.active_tab];
}

TabState& PcmanfmApp::active_tab(void) {
    return cur_tab(m_active_pane);
}

void PcmanfmApp::open_new_tab(int pane_idx, const char* path) {
    TabState tab;
    tab.path = path ? path : "/";
    tab.cursor_idx = 0;
    tab.needs_refresh = true;
    m_panes[pane_idx].tabs.push_back(tab);
    m_panes[pane_idx].active_tab = (int)m_panes[pane_idx].tabs.size() - 1;
    refresh_tab(m_panes[pane_idx].tabs.back());
}

void PcmanfmApp::close_tab(int pane_idx, int tab_idx) {
    PaneState& p = m_panes[pane_idx];
    if (p.tabs.size() <= 1) return;
    p.tabs.erase(p.tabs.begin() + tab_idx);
    if (p.active_tab >= (int)p.tabs.size()) {
        p.active_tab = (int)p.tabs.size() - 1;
    }
}

void PcmanfmApp::navigate_to(TabState& tab, const char* path, bool push_history) {
    if (!path || !*path) return;
    if (push_history && tab.path != path) {
        tab.history_back.push_back(tab.path);
        tab.history_fwd.clear();
    }
    tab.path = path;
    tab.cursor_idx = 0;
    tab.search_filter.clear();
    refresh_tab(tab);
}

void PcmanfmApp::refresh_tab(TabState& tab) {
    tab.needs_refresh = false;
    std::string err;
    vfs_scan_directory(tab.path.c_str(), m_cfg.show_hidden, m_cfg.sort_col, m_cfg.sort_asc, tab.entries, err);
    tab.last_error = err;
    tab.selected.assign(tab.entries.size(), false);
    if (tab.entries.empty()) {
        tab.cursor_idx = 0;
    } else if (tab.cursor_idx >= (int)tab.entries.size()) {
        tab.cursor_idx = (int)tab.entries.size() - 1;
    }
}

void PcmanfmApp::gather_selected_paths(const TabState& tab, std::vector<std::string>& out_paths) {
    out_paths.clear();
    for (size_t i = 0; i < tab.entries.size(); ++i) {
        if (i < tab.selected.size() && tab.selected[i]) {
            out_paths.push_back(tab.entries[i].full_path);
        }
    }
    if (out_paths.empty() && tab.cursor_idx >= 0 && tab.cursor_idx < (int)tab.entries.size()) {
        out_paths.push_back(tab.entries[tab.cursor_idx].full_path);
    }
}

void PcmanfmApp::set_toast(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    m_status_toast = buf;
    m_toast_until_us = sceKernelGetProcessTime() + 4000000ULL;
}

void PcmanfmApp::start_ime(ImePurpose purpose, const char* title, const char* initial_text, const char* target_path) {
    m_ime_purpose = purpose;
    m_ime_target_path = target_path ? target_path : "";
    snprintf(m_ime_title, sizeof(m_ime_title), "%s", title ? title : "Input");
    snprintf(m_ime_buf, sizeof(m_ime_buf), "%s", initial_text ? initial_text : "");

    if (ps4_ime_open(m_ime_title, m_ime_buf, sizeof(m_ime_buf) - 1)) {
        m_ime_using_ps4 = true;
        m_ime_fallback_open = false;
    } else {
        m_ime_using_ps4 = false;
        m_ime_fallback_open = true;
    }
}

void PcmanfmApp::finish_ime(const char* text) {
    ImePurpose p = m_ime_purpose;
    m_ime_purpose = ImePurpose::None;
    m_ime_using_ps4 = false;
    m_ime_fallback_open = false;

    if (!text) return;
    TabState& tab = active_tab();

    if (p == ImePurpose::GoToPath && *text) {
        navigate_to(tab, text, true);
    } else if (p == ImePurpose::FilterSearch) {
        tab.search_filter = text;
        tab.cursor_idx = 0;
    } else if (p == ImePurpose::NewFolder && *text) {
        char* full = new char[2048];
        vfs_join_path(tab.path.c_str(), text, full, 2048);
        std::string err;
        if (vfs_mkdir(full, err)) {
            refresh_tab(tab);
            set_toast("Created folder: %s", text);
        } else {
            set_toast("Mkdir failed: %s", err.c_str());
        }
        delete[] full;
    } else if (p == ImePurpose::Rename && *text && !m_ime_target_path.empty()) {
        char* parent = new char[2048];
        char* new_full = new char[2048];
        vfs_parent_path(m_ime_target_path.c_str(), parent, 2048);
        vfs_join_path(parent, text, new_full, 2048);
        std::string err;
        if (vfs_rename(m_ime_target_path.c_str(), new_full, err)) {
            refresh_tab(tab);
            set_toast("Renamed to %s", text);
        } else {
            set_toast("Rename failed: %s", err.c_str());
        }
        delete[] parent;
        delete[] new_full;
    } else if (p == ImePurpose::AddBookmark && *text) {
        BookmarkItem bm;
        bm.label = text;
        bm.path = m_ime_target_path;
        m_bookmarks.push_back(bm);
        vfs_save_bookmarks(m_bookmarks);
        set_toast("Bookmarked %s", bm.label.c_str());
    }
}

void PcmanfmApp::poll_ime(void) {
    if (!m_ime_using_ps4) return;
    char out_buf[1024];
    ImePollState st = ps4_ime_poll(out_buf, sizeof(out_buf));
    if (st == ImePollState::CompletedOk) {
        finish_ime(out_buf);
    } else if (st == ImePollState::Canceled || st == ImePollState::Failed) {
        m_ime_purpose = ImePurpose::None;
        m_ime_using_ps4 = false;
    }
}

void PcmanfmApp::open_entry(TabState& tab, int idx) {
    if (idx < 0 || idx >= (int)tab.entries.size()) return;
    const DirEntry& ent = tab.entries[idx];
    if (ent.is_dir) {
        navigate_to(tab, ent.full_path.c_str(), true);
    } else {
        m_inspect_entry = ent;
        m_preview_data.clear();
        m_preview_error.clear();
        m_preview_total_size = 0;
        vfs_read_file_preview(ent.full_path.c_str(), 65536, m_preview_data, m_preview_total_size, m_preview_error);
        if (ent.category == FileCategory::Text || ent.size <= 4096) {
            bool looks_text = true;
            for (size_t i = 0; i < m_preview_data.size() && i < 512; ++i) {
                uint8_t c = m_preview_data[i];
                if (c == 0) { looks_text = false; break; }
            }
            if (looks_text) m_show_text_viewer = true;
            else m_show_hex_viewer = true;
        } else {
            m_show_hex_viewer = true;
        }
    }
}

void PcmanfmApp::do_copy(bool cut) {
    TabState& tab = active_tab();
    gather_selected_paths(tab, m_clipboard_paths);
    m_clipboard_is_cut = cut;
    set_toast("%s %zu item(s)", cut ? "Cut" : "Copied", m_clipboard_paths.size());
}

void PcmanfmApp::do_paste(void) {
    if (m_clipboard_paths.empty()) {
        set_toast("Clipboard is empty");
        return;
    }
    TabState& tab = active_tab();
    FileOpType op = m_clipboard_is_cut ? FileOpType::Move : FileOpType::Copy;
    if (vfs_start_file_op(op, m_clipboard_paths, tab.path.c_str(), &m_op_prog)) {
        m_show_op_modal = true;
        if (m_clipboard_is_cut) {
            m_clipboard_paths.clear();
            m_clipboard_is_cut = false;
        }
    } else {
        set_toast("Cannot start operation");
    }
}

void PcmanfmApp::do_delete_confirmed(void) {
    TabState& tab = active_tab();
    std::vector<std::string> targets;
    gather_selected_paths(tab, targets);
    if (targets.empty()) return;
    if (vfs_start_file_op(FileOpType::Delete, targets, "", &m_op_prog)) {
        m_show_op_modal = true;
    }
}

void PcmanfmApp::handle_sdl_event(const SDL_Event& ev) {
    if (ev.type == SDL_QUIT) {
        m_exit_requested = true;
        return;
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
        bool any_modal = m_show_actions_menu || m_show_delete_confirm || m_show_properties ||
                         m_show_text_viewer || m_show_hex_viewer || m_show_prefs ||
                         m_show_help || m_show_op_modal || m_ime_fallback_open || m_ime_using_ps4;

        if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
            if (m_show_actions_menu) { m_show_actions_menu = false; return; }
            if (m_show_delete_confirm) { m_show_delete_confirm = false; return; }
            if (m_show_properties) { m_show_properties = false; return; }
            if (m_show_text_viewer) { m_show_text_viewer = false; return; }
            if (m_show_hex_viewer) { m_show_hex_viewer = false; return; }
            if (m_show_prefs) { m_show_prefs = false; return; }
            if (m_show_help) { m_show_help = false; return; }
            if (!any_modal) {
                TabState& tab = active_tab();
                char* parent = new char[2048];
                vfs_parent_path(tab.path.c_str(), parent, 2048);
                if (tab.path != parent) {
                    navigate_to(tab, parent, true);
                }
                delete[] parent;
            }
            return;
        }

        if (any_modal) return;

        TabState& tab = active_tab();
        switch (ev.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (!tab.entries.empty()) {
                    tab.cursor_idx = (tab.cursor_idx > 0) ? (tab.cursor_idx - 1) : ((int)tab.entries.size() - 1);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (!tab.entries.empty()) {
                    tab.cursor_idx = (tab.cursor_idx + 1) % (int)tab.entries.size();
                }
                break;
            case SDL_CONTROLLER_BUTTON_A: // Cross
                open_entry(tab, tab.cursor_idx);
                break;
            case SDL_CONTROLLER_BUTTON_X: // Square -> Toggle selection
                if (tab.cursor_idx >= 0 && tab.cursor_idx < (int)tab.selected.size()) {
                    tab.selected[tab.cursor_idx] = !tab.selected[tab.cursor_idx];
                }
                break;
            case SDL_CONTROLLER_BUTTON_Y: // Triangle -> Actions menu
                m_show_actions_menu = true;
                break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: { // L1 -> Prev tab
                PaneState& p = m_panes[m_active_pane];
                if (p.tabs.size() > 1) {
                    p.active_tab = (p.active_tab - 1 + (int)p.tabs.size()) % (int)p.tabs.size();
                }
                break;
            }
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: { // R1 -> Next tab
                PaneState& p = m_panes[m_active_pane];
                if (p.tabs.size() > 1) {
                    p.active_tab = (p.active_tab + 1) % (int)p.tabs.size();
                }
                break;
            }
            case SDL_CONTROLLER_BUTTON_BACK: // Select -> Toggle Hidden
                m_cfg.show_hidden = !m_cfg.show_hidden;
                refresh_tab(m_panes[0].tabs[m_panes[0].active_tab]);
                if (m_cfg.dual_pane) refresh_tab(m_panes[1].tabs[m_panes[1].active_tab]);
                set_toast("Hidden files: %s", m_cfg.show_hidden ? "Shown" : "Hidden");
                break;
            case SDL_CONTROLLER_BUTTON_START: // Options -> Preferences
                m_show_prefs = true;
                break;
            default:
                break;
        }
    } else if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_F3) {
            m_cfg.dual_pane = !m_cfg.dual_pane;
        } else if (ev.key.keysym.sym == SDLK_F5) {
            refresh_tab(active_tab());
        } else if (ev.key.keysym.sym == SDLK_h && (ev.key.keysym.mod & KMOD_CTRL)) {
            m_cfg.show_hidden = !m_cfg.show_hidden;
            refresh_tab(active_tab());
        }
    }
}

void PcmanfmApp::draw_menu_bar(void) {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Tab", "Ctrl+T")) {
                open_new_tab(m_active_pane, active_tab().path.c_str());
            }
            if (ImGui::MenuItem("New Folder...", "F7")) {
                start_ime(ImePurpose::NewFolder, "New Folder Name", "NewFolder");
            }
            if (ImGui::MenuItem("Properties")) {
                TabState& tab = active_tab();
                if (tab.cursor_idx >= 0 && tab.cursor_idx < (int)tab.entries.size()) {
                    m_inspect_entry = tab.entries[tab.cursor_idx];
                    m_show_properties = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit to XMB")) {
                m_exit_requested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Cut", "Ctrl+X")) do_copy(true);
            if (ImGui::MenuItem("Copy", "Ctrl+C")) do_copy(false);
            if (ImGui::MenuItem("Paste", "Ctrl+V")) do_paste();
            ImGui::Separator();
            if (ImGui::MenuItem("Rename...", "F2")) {
                TabState& tab = active_tab();
                if (tab.cursor_idx >= 0 && tab.cursor_idx < (int)tab.entries.size()) {
                    const DirEntry& ent = tab.entries[tab.cursor_idx];
                    start_ime(ImePurpose::Rename, "Rename Item", ent.name.c_str(), ent.full_path.c_str());
                }
            }
            if (ImGui::MenuItem("Delete", "Del")) {
                if (m_cfg.confirm_delete) m_show_delete_confirm = true;
                else do_delete_confirmed();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All")) {
                TabState& tab = active_tab();
                for (size_t i = 0; i < tab.selected.size(); ++i) tab.selected[i] = true;
            }
            if (ImGui::MenuItem("Clear Selection")) {
                TabState& tab = active_tab();
                for (size_t i = 0; i < tab.selected.size(); ++i) tab.selected[i] = false;
            }
            if (ImGui::MenuItem("Preferences")) m_show_prefs = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reload", "F5")) refresh_tab(active_tab());
            if (ImGui::MenuItem("Show Hidden Files", "Ctrl+H", &m_cfg.show_hidden)) {
                refresh_tab(active_tab());
            }
            if (ImGui::MenuItem("Dual Pane", "F3", &m_cfg.dual_pane)) {
                if (m_cfg.dual_pane) refresh_tab(cur_tab(1));
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Detailed List", NULL, m_cfg.view_mode == ViewMode::Detailed)) m_cfg.view_mode = ViewMode::Detailed;
            if (ImGui::MenuItem("Compact View", NULL, m_cfg.view_mode == ViewMode::Compact)) m_cfg.view_mode = ViewMode::Compact;
            if (ImGui::MenuItem("Icon View", NULL, m_cfg.view_mode == ViewMode::Icons)) m_cfg.view_mode = ViewMode::Icons;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Go")) {
            if (ImGui::MenuItem("Data (/data)")) navigate_to(active_tab(), "/data");
            if (ImGui::MenuItem("PKG Staging (/data/pkg)")) navigate_to(active_tab(), "/data/pkg");
            if (ImGui::MenuItem("USB 0 (/mnt/usb0)")) navigate_to(active_tab(), "/mnt/usb0");
            if (ImGui::MenuItem("USB 1 (/mnt/usb1)")) navigate_to(active_tab(), "/mnt/usb1");
            if (ImGui::MenuItem("User (/user)")) navigate_to(active_tab(), "/user");
            if (ImGui::MenuItem("App Assets (/app0)")) navigate_to(active_tab(), "/app0");
            if (ImGui::MenuItem("Root (/)")) navigate_to(active_tab(), "/");
            ImGui::Separator();
            if (ImGui::MenuItem("Go to Path...")) {
                start_ime(ImePurpose::GoToPath, "Go to Path", active_tab().path.c_str());
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Bookmarks")) {
            if (ImGui::MenuItem("Bookmark Current Folder")) {
                const char* cur = active_tab().path.c_str();
                const char* base = strrchr(cur, '/');
                start_ime(ImePurpose::AddBookmark, "Bookmark Name", (base && base[1]) ? base + 1 : cur, cur);
            }
            ImGui::Separator();
            for (size_t i = 0; i < m_bookmarks.size(); ++i) {
                if (ImGui::MenuItem(m_bookmarks[i].label.c_str())) {
                    navigate_to(active_tab(), m_bookmarks[i].path.c_str());
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("View Verbose Log (/data/pcmanfm/log.txt)")) {
                m_inspect_entry.name = "log.txt";
                m_inspect_entry.full_path = log_file_path();
                m_preview_data.clear();
                m_preview_error.clear();
                vfs_read_file_preview(log_file_path(), 65536, m_preview_data, m_preview_total_size, m_preview_error);
                m_show_text_viewer = true;
            }
            if (ImGui::MenuItem("Actions Menu (Triangle)")) m_show_actions_menu = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Controls & About")) m_show_help = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void PcmanfmApp::draw_toolbar(void) {
    TabState& tab = active_tab();

    ImGui::BeginDisabled(tab.history_back.empty());
    if (ImGui::Button("< Back")) {
        tab.history_fwd.push_back(tab.path);
        std::string prev = tab.history_back.back();
        tab.history_back.pop_back();
        navigate_to(tab, prev.c_str(), false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(tab.history_fwd.empty());
    if (ImGui::Button("Fwd >")) {
        tab.history_back.push_back(tab.path);
        std::string nxt = tab.history_fwd.back();
        tab.history_fwd.pop_back();
        navigate_to(tab, nxt.c_str(), false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    if (ImGui::Button("^ Up")) {
        char* parent = new char[2048];
        vfs_parent_path(tab.path.c_str(), parent, 2048);
        if (tab.path != parent) navigate_to(tab, parent, true);
        delete[] parent;
    }
    ImGui::SameLine();

    if (ImGui::Button("Home")) navigate_to(tab, "/data", true);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refresh_tab(tab);
    ImGui::SameLine();

    // Path bar button (opens IME)
    char path_btn[600];
    snprintf(path_btn, sizeof(path_btn), "Path: %s##pathbar", tab.path.c_str());
    float avail = ImGui::GetContentRegionAvail().x;
    float right_controls = 460.0f;
    float path_w = avail > right_controls + 200.0f ? avail - right_controls : 220.0f;
    if (ImGui::Button(path_btn, ImVec2(path_w, 0))) {
        start_ime(ImePurpose::GoToPath, "Enter Directory Path", tab.path.c_str());
    }
    ImGui::SameLine();

    if (ImGui::Button(tab.search_filter.empty() ? "Filter" : "Clear Filter")) {
        if (tab.search_filter.empty()) {
            start_ime(ImePurpose::FilterSearch, "Filter Filename", "");
        } else {
            tab.search_filter.clear();
        }
    }
    ImGui::SameLine();

    const char* vm_label = (m_cfg.view_mode == ViewMode::Detailed) ? "List"
                         : (m_cfg.view_mode == ViewMode::Compact) ? "Compact" : "Icons";
    if (ImGui::Button(vm_label)) {
        m_cfg.view_mode = (ViewMode)(((int)m_cfg.view_mode + 1) % 3);
    }
    ImGui::SameLine();

    if (ImGui::Button(m_cfg.dual_pane ? "Single Pane" : "Dual Pane")) {
        m_cfg.dual_pane = !m_cfg.dual_pane;
        if (m_cfg.dual_pane) refresh_tab(cur_tab(1));
    }
    ImGui::SameLine();

    if (ImGui::Button("Actions")) {
        m_show_actions_menu = true;
    }
}

void PcmanfmApp::draw_tree_node(const char* path, const char* label, int depth) {
    if (depth > 3) return;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (active_tab().path == path) flags |= ImGuiTreeNodeFlags_Selected;

    bool opened = ImGui::TreeNodeEx(path, flags, "%s", label);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        navigate_to(active_tab(), path, true);
    }
    if (opened) {
        std::vector<DirEntry> kids;
        std::string err;
        if (vfs_scan_directory(path, m_cfg.show_hidden, SortColumn::Name, true, kids, err)) {
            int shown = 0;
            for (size_t i = 0; i < kids.size() && shown < 40; ++i) {
                if (kids[i].is_dir) {
                    draw_tree_node(kids[i].full_path.c_str(), kids[i].name.c_str(), depth + 1);
                    shown++;
                }
            }
        }
        ImGui::TreePop();
    }
}

void PcmanfmApp::draw_sidebar(float height) {
    ImGui::BeginChild("Sidebar", ImVec2(280.0f, height), true);

    ImGui::TextDisabled("PLACES");
    struct Place { const char* name; const char* path; };
    static const Place places[] = {
        {"Data (/data)", "/data"},
        {"PKG (/data/pkg)", "/data/pkg"},
        {"USB 0 (/mnt/usb0)", "/mnt/usb0"},
        {"USB 1 (/mnt/usb1)", "/mnt/usb1"},
        {"User (/user)", "/user"},
        {"Assets (/app0)", "/app0"},
        {"Root (/)", "/"}
    };
    for (int i = 0; i < (int)(sizeof(places) / sizeof(places[0])); ++i) {
        bool sel = (active_tab().path == places[i].path);
        if (ImGui::Selectable(places[i].name, sel)) {
            navigate_to(active_tab(), places[i].path, true);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("BOOKMARKS");
    for (size_t i = 0; i < m_bookmarks.size(); ++i) {
        bool sel = (active_tab().path == m_bookmarks[i].path);
        char label[160];
        snprintf(label, sizeof(label), "* %s##bm%zu", m_bookmarks[i].label.c_str(), i);
        if (ImGui::Selectable(label, sel)) {
            navigate_to(active_tab(), m_bookmarks[i].path.c_str(), true);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("DIRECTORY TREE");
    draw_tree_node("/data", "/data", 0);
    draw_tree_node("/mnt/usb0", "/mnt/usb0", 0);

    ImGui::EndChild();
}

void PcmanfmApp::draw_file_list(int pane_idx, TabState& tab) {
    ImGuiTableFlags tflags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                             ImGuiTableFlags_BordersInnerV;

    char tid[32];
    snprintf(tid, sizeof(tid), "FileTable_%d", pane_idx);
    if (ImGui::BeginTable(tid, 5, tflags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 125.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 145.0f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty && specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                SortColumn new_col = SortColumn::Name;
                if (s.ColumnIndex == 1) new_col = SortColumn::Size;
                else if (s.ColumnIndex == 2) new_col = SortColumn::Type;
                else if (s.ColumnIndex == 4) new_col = SortColumn::Modified;
                m_cfg.sort_col = new_col;
                m_cfg.sort_asc = (s.SortDirection == ImGuiSortDirection_Ascending);
                refresh_tab(tab);
                specs->SpecsDirty = false;
            }
        }

        for (int i = 0; i < (int)tab.entries.size(); ++i) {
            const DirEntry& ent = tab.entries[i];
            if (!tab.search_filter.empty() && !str_icontains(ent.name.c_str(), tab.search_filter.c_str())) {
                continue;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool is_cur = (pane_idx == m_active_pane && tab.cursor_idx == i);
            bool is_sel = (i < (int)tab.selected.size() && tab.selected[i]);

            ImVec4 col = category_color(ent.category, m_cfg.dark_theme);
            ImGui::PushStyleColor(ImGuiCol_Text, col);

            char row_label[600];
            snprintf(row_label, sizeof(row_label), "%s %s%s##row_%d_%d",
                     category_badge(ent.category),
                     is_sel ? "[*] " : "",
                     ent.name.c_str(), pane_idx, i);

            if (ImGui::Selectable(row_label, is_cur || is_sel,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                m_active_pane = pane_idx;
                tab.cursor_idx = i;
                if (ImGui::GetIO().KeyCtrl) {
                    tab.selected[i] = !tab.selected[i];
                } else if (ImGui::IsMouseDoubleClicked(0)) {
                    open_entry(tab, i);
                }
            }
            if (ImGui::IsItemClicked(1)) {
                m_active_pane = pane_idx;
                tab.cursor_idx = i;
                m_show_actions_menu = true;
            }
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            if (ent.is_dir) {
                ImGui::TextDisabled("<DIR>");
            } else {
                char sz[48];
                vfs_format_size(ent.size, sz, sizeof(sz));
                ImGui::TextUnformatted(sz);
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(vfs_category_name(ent.category));

            ImGui::TableNextColumn();
            char mbuf[16];
            vfs_format_mode(ent.mode, ent.is_dir, ent.is_symlink, mbuf, sizeof(mbuf));
            ImGui::TextUnformatted(mbuf);

            ImGui::TableNextColumn();
            if (ent.mtime > 0) {
                time_t t = (time_t)ent.mtime;
                struct tm tm_buf;
                localtime_r(&t, &tm_buf);
                char tbuf[32];
                strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tm_buf);
                ImGui::TextUnformatted(tbuf);
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
}

void PcmanfmApp::draw_file_compact(int pane_idx, TabState& tab) {
    ImGui::BeginChild("CompactScroll", ImVec2(0, 0), false);
    int cols = m_cfg.dual_pane ? 2 : 3;
    if (ImGui::BeginTable("CompactTbl", cols, ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < (int)tab.entries.size(); ++i) {
            const DirEntry& ent = tab.entries[i];
            if (!tab.search_filter.empty() && !str_icontains(ent.name.c_str(), tab.search_filter.c_str())) continue;
            ImGui::TableNextColumn();
            bool is_cur = (pane_idx == m_active_pane && tab.cursor_idx == i);
            bool is_sel = (i < (int)tab.selected.size() && tab.selected[i]);
            ImGui::PushStyleColor(ImGuiCol_Text, category_color(ent.category, m_cfg.dark_theme));
            char label[300];
            snprintf(label, sizeof(label), "%s %s##c_%d_%d", category_badge(ent.category), ent.name.c_str(), pane_idx, i);
            if (ImGui::Selectable(label, is_cur || is_sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_active_pane = pane_idx;
                tab.cursor_idx = i;
                if (ImGui::IsMouseDoubleClicked(0)) open_entry(tab, i);
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void PcmanfmApp::draw_file_icons(int pane_idx, TabState& tab) {
    ImGui::BeginChild("IconsScroll", ImVec2(0, 0), false);
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = imax(2, (int)(avail / 195.0f));
    if (ImGui::BeginTable("IconsTbl", cols, ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < (int)tab.entries.size(); ++i) {
            const DirEntry& ent = tab.entries[i];
            if (!tab.search_filter.empty() && !str_icontains(ent.name.c_str(), tab.search_filter.c_str())) continue;
            ImGui::TableNextColumn();
            bool is_cur = (pane_idx == m_active_pane && tab.cursor_idx == i);
            ImGui::PushStyleColor(ImGuiCol_Button,
                is_cur ? ImVec4(0.28f, 0.48f, 0.78f, 0.85f) : ImVec4(0.18f, 0.20f, 0.25f, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_Text, category_color(ent.category, m_cfg.dark_theme));
            char tile[256];
            snprintf(tile, sizeof(tile), "%s\n%.18s##ic_%d_%d", category_badge(ent.category), ent.name.c_str(), pane_idx, i);
            if (ImGui::Button(tile, ImVec2(180.0f, 92.0f))) {
                m_active_pane = pane_idx;
                if (tab.cursor_idx == i) open_entry(tab, i);
                else tab.cursor_idx = i;
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void PcmanfmApp::draw_pane(int pane_idx, float width, float height) {
    PaneState& pane = m_panes[pane_idx];
    bool is_active = (m_active_pane == pane_idx);

    ImGui::PushStyleColor(ImGuiCol_Border,
        is_active ? ImVec4(0.32f, 0.62f, 0.96f, 1.0f) : ImVec4(0.25f, 0.25f, 0.28f, 0.6f));
    char pid[32];
    snprintf(pid, sizeof(pid), "Pane_%d", pane_idx);
    ImGui::BeginChild(pid, ImVec2(width, height), true);

    // Tab bar
    char tbar[32];
    snprintf(tbar, sizeof(tbar), "Tabs_%d", pane_idx);
    if (ImGui::BeginTabBar(tbar, ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (int t = 0; t < (int)pane.tabs.size(); ++t) {
            bool open = true;
            bool* p_open = (pane.tabs.size() > 1) ? &open : NULL;
            const char* pstr = pane.tabs[t].path.c_str();
            const char* base = strrchr(pstr, '/');
            const char* tname = (base && base[1]) ? (base + 1) : pstr;
            char tlabel[128];
            snprintf(tlabel, sizeof(tlabel), "%s##t_%d_%d", tname, pane_idx, t);
            if (ImGui::BeginTabItem(tlabel, p_open)) {
                if (pane.active_tab != t) {
                    pane.active_tab = t;
                }
                ImGui::EndTabItem();
            }
            if (!open) {
                close_tab(pane_idx, t);
                break;
            }
        }
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
            open_new_tab(pane_idx, cur_tab(pane_idx).path.c_str());
        }
        ImGui::EndTabBar();
    }

    TabState& tab = cur_tab(pane_idx);
    if (!tab.last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", tab.last_error.c_str());
    } else if (m_cfg.view_mode == ViewMode::Detailed) {
        draw_file_list(pane_idx, tab);
    } else if (m_cfg.view_mode == ViewMode::Compact) {
        draw_file_compact(pane_idx, tab);
    } else {
        draw_file_icons(pane_idx, tab);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void PcmanfmApp::draw_status_bar(void) {
    TabState& tab = active_tab();
    size_t sel_count = 0;
    uint64_t sel_bytes = 0;
    for (size_t i = 0; i < tab.entries.size(); ++i) {
        if (i < tab.selected.size() && tab.selected[i]) {
            sel_count++;
            sel_bytes += tab.entries[i].size;
        }
    }

    uint64_t total_sp = 0, free_sp = 0;
    vfs_get_disk_space(tab.path.c_str(), total_sp, free_sp);
    char free_buf[48], total_buf[48], sel_buf[48];
    vfs_format_size(free_sp, free_buf, sizeof(free_buf));
    vfs_format_size(total_sp, total_buf, sizeof(total_buf));
    vfs_format_size(sel_bytes, sel_buf, sizeof(sel_buf));

    ImGui::Separator();
    uint64_t now = sceKernelGetProcessTime();
    if (!m_status_toast.empty() && now < m_toast_until_us) {
        ImGui::TextColored(ImVec4(0.45f, 0.88f, 0.98f, 1.0f), "%s", m_status_toast.c_str());
    } else {
        ImGui::Text("%zu items | Selected: %zu (%s) | Clipboard: %zu (%s) | Free: %s / %s",
                    tab.entries.size(),
                    sel_count, sel_buf,
                    m_clipboard_paths.size(),
                    m_clipboard_is_cut ? "Cut" : "Copy",
                    free_buf, total_buf);
    }
}

void PcmanfmApp::draw_modals(void) {
    TabState& tab = active_tab();

    if (m_show_actions_menu) ImGui::OpenPopup("ActionsMenu");
    if (ImGui::BeginPopupModal("ActionsMenu", &m_show_actions_menu, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::Button("Copy", ImVec2(280, 0))) { do_copy(false); m_show_actions_menu = false; }
        if (ImGui::Button("Cut", ImVec2(280, 0))) { do_copy(true); m_show_actions_menu = false; }
        if (ImGui::Button("Paste Here", ImVec2(280, 0))) { do_paste(); m_show_actions_menu = false; }
        ImGui::Separator();
        if (ImGui::Button("New Folder...", ImVec2(280, 0))) {
            m_show_actions_menu = false;
            start_ime(ImePurpose::NewFolder, "New Folder Name", "NewFolder");
        }
        if (ImGui::Button("Rename...", ImVec2(280, 0))) {
            m_show_actions_menu = false;
            if (tab.cursor_idx >= 0 && tab.cursor_idx < (int)tab.entries.size()) {
                const DirEntry& ent = tab.entries[tab.cursor_idx];
                start_ime(ImePurpose::Rename, "Rename Item", ent.name.c_str(), ent.full_path.c_str());
            }
        }
        if (ImGui::Button("Delete", ImVec2(280, 0))) {
            m_show_actions_menu = false;
            if (m_cfg.confirm_delete) m_show_delete_confirm = true;
            else do_delete_confirmed();
        }
        ImGui::Separator();
        if (ImGui::Button("Properties", ImVec2(280, 0))) {
            m_show_actions_menu = false;
            if (tab.cursor_idx >= 0 && tab.cursor_idx < (int)tab.entries.size()) {
                m_inspect_entry = tab.entries[tab.cursor_idx];
                m_show_properties = true;
            }
        }
        if (ImGui::Button("Go to Path...", ImVec2(280, 0))) {
            m_show_actions_menu = false;
            start_ime(ImePurpose::GoToPath, "Go to Path", tab.path.c_str());
        }
        if (ImGui::Button("Close (Circle)", ImVec2(280, 0))) m_show_actions_menu = false;
        ImGui::EndPopup();
    }

    if (m_show_delete_confirm) ImGui::OpenPopup("Confirm Delete");
    if (ImGui::BeginPopupModal("Confirm Delete", &m_show_delete_confirm, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::vector<std::string> targets;
        gather_selected_paths(tab, targets);
        ImGui::Text("Permanently delete %zu selected item(s)?", targets.size());
        if (!targets.empty()) {
            ImGui::TextDisabled("%s%s", targets[0].c_str(), targets.size() > 1 ? " (+ more)" : "");
        }
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(140, 0))) {
            m_show_delete_confirm = false;
            do_delete_confirmed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140, 0))) {
            m_show_delete_confirm = false;
        }
        ImGui::EndPopup();
    }

    if (m_show_op_modal) {
        ImGui::OpenPopup("File Operation");
        if (__atomic_load_n(&m_op_prog.done, __ATOMIC_ACQUIRE)) {
            vfs_poll_file_op_join(&m_op_prog);
            m_show_op_modal = false;
            refresh_tab(m_panes[0].tabs[m_panes[0].active_tab]);
            if (m_cfg.dual_pane) refresh_tab(m_panes[1].tabs[m_panes[1].active_tab]);
            if (__atomic_load_n(&m_op_prog.ok, __ATOMIC_ACQUIRE)) {
                set_toast("Operation completed (%llu items)", (unsigned long long)m_op_prog.items_done);
            } else {
                set_toast("Operation error: %s", m_op_prog.error_msg);
            }
        }
    }
    if (ImGui::BeginPopupModal("File Operation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* opname = (m_op_prog.type == FileOpType::Copy) ? "Copying..."
                           : (m_op_prog.type == FileOpType::Move) ? "Moving..." : "Deleting...";
        ImGui::Text("%s", opname);
        ImGui::Text("Current: %s", m_op_prog.current_file);
        char bbuf[48];
        vfs_format_size(m_op_prog.bytes_done, bbuf, sizeof(bbuf));
        ImGui::Text("Items: %llu | Bytes: %s", (unsigned long long)m_op_prog.items_done, bbuf);
        if (ImGui::Button("Cancel", ImVec2(160, 0))) {
            __atomic_store_n(&m_op_prog.cancel_req, 1, __ATOMIC_RELEASE);
        }
        ImGui::EndPopup();
    }

    if (m_show_properties) ImGui::OpenPopup("Properties");
    if (ImGui::BeginPopupModal("Properties", &m_show_properties, ImGuiWindowFlags_AlwaysAutoResize)) {
        char sz[48], mbuf[16];
        vfs_format_size(m_inspect_entry.size, sz, sizeof(sz));
        vfs_format_mode(m_inspect_entry.mode, m_inspect_entry.is_dir, m_inspect_entry.is_symlink, mbuf, sizeof(mbuf));
        ImGui::Text("Name: %s", m_inspect_entry.name.c_str());
        ImGui::Text("Path: %s", m_inspect_entry.full_path.c_str());
        ImGui::Text("Type: %s", vfs_category_name(m_inspect_entry.category));
        ImGui::Text("Size: %s (%llu bytes)", sz, (unsigned long long)m_inspect_entry.size);
        ImGui::Text("Mode: %s (0%o)", mbuf, m_inspect_entry.mode);
        ImGui::Separator();
        if (ImGui::Button("View as Text", ImVec2(150, 0))) {
            m_show_properties = false;
            m_preview_data.clear();
            vfs_read_file_preview(m_inspect_entry.full_path.c_str(), 65536, m_preview_data, m_preview_total_size, m_preview_error);
            m_show_text_viewer = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Hex View", ImVec2(150, 0))) {
            m_show_properties = false;
            m_preview_data.clear();
            vfs_read_file_preview(m_inspect_entry.full_path.c_str(), 16384, m_preview_data, m_preview_total_size, m_preview_error);
            m_show_hex_viewer = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120, 0))) m_show_properties = false;
        ImGui::EndPopup();
    }

    if (m_show_text_viewer) ImGui::OpenPopup("Text Viewer");
    ImGui::SetNextWindowSize(ImVec2(1280, 760), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Text Viewer", &m_show_text_viewer)) {
        ImGui::Text("File: %s (%llu bytes, showing %zu bytes)",
                    m_inspect_entry.full_path.c_str(),
                    (unsigned long long)m_preview_total_size,
                    m_preview_data.size());
        ImGui::Separator();
        ImGui::BeginChild("TextBody", ImVec2(0, -50), true);
        if (!m_preview_error.empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_preview_error.c_str());
        } else if (!m_preview_data.empty()) {
            ImGui::TextUnformatted((const char*)m_preview_data.data(),
                                   (const char*)m_preview_data.data() + m_preview_data.size());
        } else {
            ImGui::TextDisabled("(Empty file)");
        }
        ImGui::EndChild();
        if (ImGui::Button("Switch to Hex", ImVec2(160, 0))) {
            m_show_text_viewer = false;
            m_show_hex_viewer = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(140, 0))) m_show_text_viewer = false;
        ImGui::EndPopup();
    }

    if (m_show_hex_viewer) ImGui::OpenPopup("Hex Viewer");
    ImGui::SetNextWindowSize(ImVec2(1280, 760), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Hex Viewer", &m_show_hex_viewer)) {
        ImGui::Text("Hex: %s (%llu bytes)", m_inspect_entry.full_path.c_str(), (unsigned long long)m_preview_total_size);
        ImGui::Separator();
        ImGui::BeginChild("HexBody", ImVec2(0, -50), true);
        for (size_t off = 0; off < m_preview_data.size(); off += 16) {
            char line[128];
            int pos = snprintf(line, sizeof(line), "%08zX: ", off);
            for (size_t j = 0; j < 16; ++j) {
                if (off + j < m_preview_data.size()) {
                    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", m_preview_data[off + j]);
                } else {
                    pos += snprintf(line + pos, sizeof(line) - pos, "   ");
                }
            }
            pos += snprintf(line + pos, sizeof(line) - pos, " |");
            for (size_t j = 0; j < 16 && off + j < m_preview_data.size(); ++j) {
                uint8_t c = m_preview_data[off + j];
                line[pos++] = (c >= 32 && c < 127) ? (char)c : '.';
            }
            line[pos++] = '|';
            line[pos] = '\0';
            ImGui::TextUnformatted(line);
        }
        ImGui::EndChild();
        if (ImGui::Button("Close", ImVec2(140, 0))) m_show_hex_viewer = false;
        ImGui::EndPopup();
    }

    if (m_show_prefs) ImGui::OpenPopup("Preferences");
    if (ImGui::BeginPopupModal("Preferences", &m_show_prefs, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::Checkbox("Dark Theme", &m_cfg.dark_theme)) apply_theme();
        if (ImGui::Checkbox("Show Hidden Files", &m_cfg.show_hidden)) {
            refresh_tab(m_panes[0].tabs[m_panes[0].active_tab]);
            if (m_cfg.dual_pane) refresh_tab(m_panes[1].tabs[m_panes[1].active_tab]);
        }
        ImGui::Checkbox("Dual Pane Mode", &m_cfg.dual_pane);
        ImGui::Checkbox("Confirm Before Delete", &m_cfg.confirm_delete);
        if (ImGui::SliderFloat("UI Scale", &m_cfg.ui_scale, 1.1f, 2.2f, "%.2f")) {
            ImGui::GetIO().FontGlobalScale = m_cfg.ui_scale;
        }
        ImGui::Separator();
        if (ImGui::Button("Save & Close", ImVec2(160, 0))) {
            save_config();
            m_show_prefs = false;
        }
        ImGui::EndPopup();
    }

    if (m_show_help) ImGui::OpenPopup("PCManFM-PS4 Help");
    if (ImGui::BeginPopupModal("PCManFM-PS4 Help", &m_show_help, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("PCManFM-PS4 v01.01 (Orbis OS File Manager)");
        ImGui::Separator();
        ImGui::Text("D-Pad / Left Stick : Navigate list & UI");
        ImGui::Text("Cross (X)          : Open folder / Preview file");
        ImGui::Text("Circle (O)         : Go up / Close dialog");
        ImGui::Text("Square ([])        : Toggle item selection");
        ImGui::Text("Triangle (/\\)      : Actions menu (Copy/Cut/Paste/Rename/Delete)");
        ImGui::Text("L1 / R1            : Switch tabs");
        ImGui::Text("Select / Share     : Toggle hidden files");
        ImGui::Text("Options            : Preferences");
        ImGui::Text("FTP Debug Log      : /data/pcmanfm/log.txt");
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(140, 0))) m_show_help = false;
        ImGui::EndPopup();
    }

    if (m_ime_fallback_open) ImGui::OpenPopup("Text Input");
    if (ImGui::BeginPopupModal("Text Input", &m_ime_fallback_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", m_ime_title);
        ImGui::InputText("##ime_fb", m_ime_buf, sizeof(m_ime_buf));
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            finish_ime(m_ime_buf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            finish_ime(NULL);
        }
        ImGui::EndPopup();
    }
}

void PcmanfmApp::render_frame(void) {
    poll_ime();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("PCManFM-PS4", NULL, root_flags);
    draw_menu_bar();
    draw_toolbar();
    ImGui::Separator();

    float body_h = ImGui::GetContentRegionAvail().y - 42.0f;
    if (body_h < 120.0f) body_h = 120.0f;

    draw_sidebar(body_h);
    ImGui::SameLine();

    float panes_w = ImGui::GetContentRegionAvail().x;
    if (m_cfg.dual_pane) {
        float half_w = (panes_w - 8.0f) * 0.5f;
        draw_pane(0, half_w, body_h);
        ImGui::SameLine();
        draw_pane(1, half_w, body_h);
    } else {
        draw_pane(0, panes_w, body_h);
    }

    draw_status_bar();
    draw_modals();
    ImGui::End();
}
