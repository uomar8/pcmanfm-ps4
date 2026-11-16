#include "ps4_ime.h"
#include "log.h"

#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <string.h>

static bool g_ime_sys_ready = false;
static bool g_ime_active = false;
static int32_t g_user_id = -1;

// Orbis SceImeDialog expects UTF-16 code units, even though OpenOrbis headers declare wchar_t*.
static uint16_t g_title_u16[128];
static uint16_t g_text_u16[1024];

static void utf8_to_utf16(const char* src, uint16_t* dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t di = 0;
    const unsigned char* s = (const unsigned char*)src;
    while (*s && di + 1 < dst_cap) {
        uint32_t cp = 0;
        if (*s < 0x80) {
            cp = *s++;
        } else if ((*s & 0xE0) == 0xC0 && s[1]) {
            cp = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
            cp = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else {
            cp = '?';
            s++;
        }
        if (cp <= 0xFFFF) {
            dst[di++] = (uint16_t)cp;
        }
    }
    dst[di] = 0;
}

static void utf16_to_utf8(const uint16_t* src, char* dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t di = 0;
    while (*src && di + 4 < dst_cap) {
        uint32_t cp = *src++;
        if (cp < 0x80) {
            dst[di++] = (char)cp;
        } else if (cp < 0x800) {
            dst[di++] = (char)(0xC0 | (cp >> 6));
            dst[di++] = (char)(0x80 | (cp & 0x3F));
        } else {
            dst[di++] = (char)(0xE0 | (cp >> 12));
            dst[di++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[di++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    dst[di] = '\0';
}

bool ps4_ime_init_system(void) {
    if (g_ime_sys_ready) return true;

    int32_t rc = sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
    log_msg("IME", "sceSysmoduleLoadModule(IME_DIALOG) -> 0x%08x", rc);

    rc = sceCommonDialogInitialize();
    log_msg("IME", "sceCommonDialogInitialize() -> 0x%08x", rc);

    OrbisUserServiceInitializeParams params;
    memset(&params, 0, sizeof(params));
    params.priority = 700;
    int32_t us_rc = sceUserServiceInitialize(&params);
    log_msg("IME", "sceUserServiceInitialize() -> 0x%08x", us_rc);

    int32_t uid_rc = sceUserServiceGetInitialUser(&g_user_id);
    log_msg("IME", "sceUserServiceGetInitialUser() -> 0x%08x (userId=%d)", uid_rc, g_user_id);
    if (uid_rc < 0) {
        g_user_id = 0;
    }

    g_ime_sys_ready = true;
    return true;
}

bool ps4_ime_open(const char* title, const char* initial_text, size_t max_chars) {
    if (!g_ime_sys_ready) {
        ps4_ime_init_system();
    }
    if (g_ime_active) {
        ps4_ime_close();
    }

    memset(g_title_u16, 0, sizeof(g_title_u16));
    memset(g_text_u16, 0, sizeof(g_text_u16));
    utf8_to_utf16(title, g_title_u16, sizeof(g_title_u16) / sizeof(g_title_u16[0]));
    utf8_to_utf16(initial_text, g_text_u16, sizeof(g_text_u16) / sizeof(g_text_u16[0]));

    if (max_chars == 0 || max_chars >= 1024) {
        max_chars = 512;
    }

    OrbisImeDialogSetting setting;
    memset(&setting, 0, sizeof(setting));
    setting.userId = (uint32_t)g_user_id;
    setting.type = ORBIS_TYPE_DEFAULT;
    setting.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    setting.inputMethod = ORBIS__DEFAULT;
    setting.maxTextLength = (uint32_t)max_chars;
    setting.inputTextBuffer = (wchar_t*)g_text_u16;
    setting.posx = 960.0f;
    setting.posy = 540.0f;
    setting.horizontalAlignment = ORBIS_H_CENTER;
    setting.verticalAlignment = ORBIS_V_CENTER;
    setting.title = (const wchar_t*)g_title_u16;

    int32_t rc = sceImeDialogInit(&setting, NULL);
    log_msg("IME", "sceImeDialogInit(title=\"%s\") -> 0x%08x", title ? title : "", rc);
    if (rc < 0) {
        return false;
    }

    g_ime_active = true;
    return true;
}

ImePollState ps4_ime_poll(char* out_utf8, size_t out_size) {
    if (!g_ime_active) return ImePollState::Idle;

    OrbisDialogStatus st = sceImeDialogGetStatus();
    if (st == ORBIS_DIALOG_STATUS_RUNNING) {
        return ImePollState::Running;
    }
    if (st == ORBIS_DIALOG_STATUS_STOPPED) {
        OrbisDialogResult res;
        memset(&res, 0, sizeof(res));
        int32_t rc = sceImeDialogGetResult(&res);
        log_msg("IME", "sceImeDialogGetResult() -> 0x%08x (endstatus=%d)", rc, (int)res.endstatus);
        ps4_ime_close();

        if (res.endstatus == ORBIS_DIALOG_OK) {
            utf16_to_utf8(g_text_u16, out_utf8, out_size);
            return ImePollState::CompletedOk;
        }
        return ImePollState::Canceled;
    }
    return ImePollState::Idle;
}

void ps4_ime_close(void) {
    if (g_ime_active) {
        int32_t rc = sceImeDialogTerm();
        log_msg("IME", "sceImeDialogTerm() -> 0x%08x", rc);
        g_ime_active = false;
    }
}
