#!/usr/bin/env python3
import pathlib
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/SDL-PS4")

# 1. SDL_video.c: do not force SDL_WINDOW_OPENGL on __PS4__
video_c = root / "src/video/SDL_video.c"
s = video_c.read_text()
old_ogl = "#if (SDL_VIDEO_OPENGL && __MACOSX__) || (__IPHONEOS__ && !TARGET_OS_MACCATALYST) || __ANDROID__ || __NACL__ || __PS4__"
new_ogl = "#if (SDL_VIDEO_OPENGL && __MACOSX__) || (__IPHONEOS__ && !TARGET_OS_MACCATALYST) || __ANDROID__ || __NACL__"
assert old_ogl in s, "SDL_video.c OGL guard not found"
video_c.write_text(s.replace(old_ogl, new_ogl, 1))

# 2. SDL_ps4video.c: add sceVideoOut software framebuffer + skip Piglet + preserve SDL_LogSetOutputFunction
ps4_c = root / "src/video/ps4/SDL_ps4video.c"
s = ps4_c.read_text()

# Add VideoOut header
old_inc = "#include <orbis/UserService.h>"
new_inc = "#include <orbis/UserService.h>\n#include <orbis/VideoOut.h>"
assert old_inc in s, "UserService include not found"
s = s.replace(old_inc, new_inc, 1)

# Guard sceUserServiceInitialize
old_user = """    ret = sceUserServiceInitialize(&param);
    if (ret != 0) {"""
new_user = """    ret = sceUserServiceInitialize(&param);
    if (ret != 0 && ret != 0x80960003) {"""
assert old_user in s, "sceUserServiceInitialize check not found"
s = s.replace(old_user, new_user, 1)

# Preserve user SDL_LogSetOutputFunction
old_log = "    // log to kernel\n    SDL_LogSetOutputFunction((SDL_LogOutputFunction) &PS4_logCb, NULL);"
new_log = "    /* keep caller SDL_LogSetOutputFunction if set */"
assert old_log in s, "PS4_logCb line not found"
s = s.replace(old_log, new_log, 1)

# Skip PigletInit in PS4_CreateDevice
old_piglet = """    // load piglet
    if (PS4_PigletInit() != 0) {
        return NULL;
    }"""
new_piglet = """    /* Piglet skipped: using sceVideoOut software framebuffer */"""
assert old_piglet in s, "PS4_PigletInit call not found"
s = s.replace(old_piglet, new_piglet, 1)

# Add framebuffer state & callbacks before PS4_CreateDevice
fb_code = """
static int32_t ps4_vo_handle = -1;
static OrbisKernelEqueue ps4_vo_equeue = NULL;
static off_t ps4_vo_dmem_off = 0;
static size_t ps4_vo_dmem_size = 0;
static void *ps4_vo_mem = NULL;
static uint8_t *ps4_vo_fb[2] = { NULL, NULL };
static int ps4_vo_active_idx = 0;
static int64_t ps4_vo_frame_id = 0;
static void *ps4_cpu_fb = NULL;
static size_t ps4_cpu_fb_size = 0;

static void PS4_DestroyWindowFramebuffer(_THIS, SDL_Window *window) {
    (void)_this;
    (void)window;
    if (ps4_cpu_fb) {
        SDL_free(ps4_cpu_fb);
        ps4_cpu_fb = NULL;
        ps4_cpu_fb_size = 0;
    }
    if (ps4_vo_handle >= 0) {
        sceVideoOutClose(ps4_vo_handle);
        ps4_vo_handle = -1;
    }
    if (ps4_vo_dmem_size > 0) {
        sceKernelReleaseDirectMemory(ps4_vo_dmem_off, ps4_vo_dmem_size);
        ps4_vo_dmem_off = 0;
        ps4_vo_dmem_size = 0;
        ps4_vo_mem = NULL;
    }
    if (ps4_vo_equeue) {
        sceKernelDeleteEqueue(ps4_vo_equeue);
        ps4_vo_equeue = NULL;
    }
}

static int PS4_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch) {
    int w = (window->w > 0) ? window->w : 1920;
    int h = (window->h > 0) ? window->h : 1080;
    int pitch_bytes = w * 4;
    size_t fb_size = (size_t)pitch_bytes * (size_t)h;
    size_t align = 0x200000;
    size_t alloc_size = (fb_size * 2 + align - 1) & ~(align - 1);
    OrbisVideoOutBufferAttribute attr;
    int rc;

    PS4_DestroyWindowFramebuffer(_this, window);

    ps4_vo_handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (ps4_vo_handle < 0) {
        return SDL_SetError("sceVideoOutOpen failed: 0x%08x", ps4_vo_handle);
    }

    rc = sceKernelCreateEqueue(&ps4_vo_equeue, "sdl_flip_queue");
    if (rc < 0) {
        PS4_DestroyWindowFramebuffer(_this, window);
        return SDL_SetError("sceKernelCreateEqueue failed: 0x%08x", rc);
    }
    sceVideoOutAddFlipEvent(ps4_vo_equeue, ps4_vo_handle, 0);
    sceVideoOutSetFlipRate(ps4_vo_handle, 0);

    rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), alloc_size, align, 3, &ps4_vo_dmem_off);
    if (rc < 0) {
        PS4_DestroyWindowFramebuffer(_this, window);
        return SDL_SetError("sceKernelAllocateDirectMemory failed: 0x%08x", rc);
    }
    ps4_vo_dmem_size = alloc_size;

    rc = sceKernelMapDirectMemory(&ps4_vo_mem, alloc_size, 0x33, 0, ps4_vo_dmem_off, align);
    if (rc < 0) {
        PS4_DestroyWindowFramebuffer(_this, window);
        return SDL_SetError("sceKernelMapDirectMemory failed: 0x%08x", rc);
    }

    ps4_vo_fb[0] = (uint8_t *)ps4_vo_mem;
    ps4_vo_fb[1] = (uint8_t *)ps4_vo_mem + fb_size;
    ps4_vo_active_idx = 0;
    ps4_vo_frame_id = 0;

    SDL_memset(&attr, 0, sizeof(attr));
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, 1, 0, w, h, w);
    rc = sceVideoOutRegisterBuffers(ps4_vo_handle, 0, (void **)ps4_vo_fb, 2, &attr);
    if (rc != 0) {
        PS4_DestroyWindowFramebuffer(_this, window);
        return SDL_SetError("sceVideoOutRegisterBuffers failed: 0x%08x", rc);
    }

    ps4_cpu_fb = SDL_calloc(1, fb_size);
    if (!ps4_cpu_fb) {
        PS4_DestroyWindowFramebuffer(_this, window);
        return SDL_OutOfMemory();
    }
    ps4_cpu_fb_size = fb_size;

    *format = SDL_PIXELFORMAT_ARGB8888;
    *pixels = ps4_cpu_fb;
    *pitch = pitch_bytes;
    SDL_Log("PS4_CreateWindowFramebuffer OK (%dx%d)", w, h);
    return 0;
}

static int PS4_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects) {
    (void)_this;
    (void)window;
    (void)rects;
    (void)numrects;
    if (ps4_vo_handle < 0 || !ps4_cpu_fb || !ps4_vo_fb[ps4_vo_active_idx]) {
        return -1;
    }

    SDL_memcpy(ps4_vo_fb[ps4_vo_active_idx], ps4_cpu_fb, ps4_cpu_fb_size);
    int64_t fid = ++ps4_vo_frame_id;
    sceVideoOutSubmitFlip(ps4_vo_handle, ps4_vo_active_idx, ORBIS_VIDEO_OUT_FLIP_VSYNC, fid);

    for (;;) {
        OrbisVideoOutFlipStatus st;
        if (sceVideoOutGetFlipStatus(ps4_vo_handle, &st) != 0) {
            break;
        }
        if (st.flipArg >= fid) {
            break;
        }
        OrbisKernelEvent evt;
        int count = 0;
        if (sceKernelWaitEqueue(ps4_vo_equeue, &evt, 1, &count, 0) != 0) {
            break;
        }
    }

    ps4_vo_active_idx = (ps4_vo_active_idx + 1) % 2;
    return 0;
}
"""

anchor = "static SDL_VideoDevice *\nPS4_CreateDevice(int devindex) {"
assert anchor in s, "PS4_CreateDevice anchor not found"
s = s.replace(anchor, fb_code + "\n" + anchor, 1)

# Register framebuffer callbacks on device
old_destroy = "    device->DestroyWindow = PS4_DestroyWindow;"
new_destroy = """    device->DestroyWindow = PS4_DestroyWindow;
    device->CreateWindowFramebuffer = PS4_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = PS4_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = PS4_DestroyWindowFramebuffer;"""
assert old_destroy in s, "DestroyWindow assignment not found"
s = s.replace(old_destroy, new_destroy, 1)

# Make EGL optional in PS4_CreateWindow
old_cw = """    if (!_this->egl_data) {
        return SDL_SetError("egl not initialized");
    }

    window_data = (SDL_WindowData *) SDL_calloc(1, sizeof(SDL_WindowData));
    if (window_data == NULL) {
        return SDL_OutOfMemory();
    }

    ps4_egl_window.uWidth = window->w;
    ps4_egl_window.uHeight = window->h;

    window_data->egl_surface = SDL_EGL_CreateSurface(_this, &ps4_egl_window);
    if (window_data->egl_surface == EGL_NO_SURFACE) {
        return SDL_SetError("could not create egl window surface");
    }"""

new_cw = """    window_data = (SDL_WindowData *) SDL_calloc(1, sizeof(SDL_WindowData));
    if (window_data == NULL) {
        return SDL_OutOfMemory();
    }

    if (window->flags & SDL_WINDOW_OPENGL) {
        if (!_this->egl_data) {
            SDL_free(window_data);
            return SDL_SetError("egl not initialized");
        }
        ps4_egl_window.uWidth = window->w;
        ps4_egl_window.uHeight = window->h;
        window_data->egl_surface = SDL_EGL_CreateSurface(_this, &ps4_egl_window);
        if (window_data->egl_surface == EGL_NO_SURFACE) {
            SDL_free(window_data);
            return SDL_SetError("could not create egl window surface");
        }
    }"""
assert old_cw in s, "PS4_CreateWindow EGL block not found"
s = s.replace(old_cw, new_cw, 1)

ps4_c.write_text(s)
print("Patched SDL-PS4 successfully for sceVideoOut software framebuffer.")
