#pragma once

#include "vfs.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

enum class ViewMode {
    Detailed = 0,
    Compact,
    Icons
};

enum class ImePurpose {
    None = 0,
    Rename,
    NewFolder,
    GoToPath,
    FilterSearch,
    AddBookmark
};

struct TabState {
    std::string path;
    std::vector<std::string> history_back;
    std::vector<std::string> history_fwd;
    std::vector<DirEntry> entries;
    std::vector<bool> selected;
    int cursor_idx;
    std::string search_filter;
    std::string last_error;
    bool needs_refresh;
};

struct PaneState {
    std::vector<TabState> tabs;
    int active_tab;
};

struct AppConfig {
    bool show_hidden;
    bool dual_pane;
    bool confirm_delete;
    bool dark_theme;
    ViewMode view_mode;
    SortColumn sort_col;
    bool sort_asc;
    float ui_scale;
};

class PcmanfmApp {
public:
    PcmanfmApp();
    ~PcmanfmApp();

    bool init(void);
    void shutdown(void);
    void handle_sdl_event(const SDL_Event& ev);
    void prepare_imgui_frame(void);
    void render_frame(void);
    bool should_exit(void) const { return m_exit_requested; }

private:
    PaneState m_panes[2];
    int m_active_pane;
    AppConfig m_cfg;

    // Clipboard
    std::vector<std::string> m_clipboard_paths;
    bool m_clipboard_is_cut;

    // Bookmarks
    std::vector<BookmarkItem> m_bookmarks;

    // Background file op
    FileOpProgress m_op_prog;
    bool m_show_op_modal;

    // Modals & viewers
    bool m_show_actions_menu;
    bool m_show_delete_confirm;
    bool m_show_properties;
    bool m_show_text_viewer;
    bool m_show_hex_viewer;
    bool m_show_prefs;
    bool m_show_help;

    DirEntry m_inspect_entry;
    std::vector<uint8_t> m_preview_data;
    uint64_t m_preview_total_size;
    std::string m_preview_error;
    std::string m_status_toast;
    uint64_t m_toast_until_us;

    // Text input (SceImeDialog + fallback modal)
    ImePurpose m_ime_purpose;
    bool m_ime_using_ps4;
    bool m_ime_fallback_open;
    char m_ime_title[128];
    char m_ime_buf[1024];
    std::string m_ime_target_path;

    // Sidebar tree root
    std::string m_tree_root;
    bool m_exit_requested;
    bool m_scroll_to_cursor;
    bool m_ui_nav_mode;
    bool m_l2_down;
    bool m_r2_down;

    // Helpers
    void load_config(void);
    void save_config(void);
    void apply_theme(void);

    TabState& cur_tab(int pane_idx);
    TabState& active_tab(void);
    void open_new_tab(int pane_idx, const char* path);
    void close_tab(int pane_idx, int tab_idx);
    void navigate_to(TabState& tab, const char* path, bool push_history = true);
    void refresh_tab(TabState& tab);

    void gather_selected_paths(const TabState& tab, std::vector<std::string>& out_paths);
    void set_toast(const char* fmt, ...);

    void start_ime(ImePurpose purpose, const char* title, const char* initial_text, const char* target_path = "");
    void finish_ime(const char* text);
    void poll_ime(void);

    void open_entry(TabState& tab, int idx);
    void do_copy(bool cut);
    void do_paste(void);
    void do_delete_confirmed(void);

    // UI sections
    void draw_menu_bar(void);
    void draw_toolbar(void);
    void draw_sidebar(float height);
    void draw_tree_node(const char* path, const char* label, int depth);
    void draw_pane(int pane_idx, float width, float height);
    void draw_file_list(int pane_idx, TabState& tab);
    void draw_file_compact(int pane_idx, TabState& tab);
    void draw_file_icons(int pane_idx, TabState& tab);
    void draw_status_bar(void);
    void draw_modals(void);
};
