#include <jni.h>
#include <android/log.h>
#include <android/input.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>

#include "pl/Hook.h"
#include "pl/Gloss.h"

#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"

#include <cstring>

/* ================= GLOBAL ================= */

static bool g_initialized = false;
static int g_width = 0, g_height = 0;

static EGLBoolean (*orig_eglswapbuffers)(EGLDisplay, EGLSurface) = nullptr;

/* ================= ENCHANT ================= */

static bool enchant_enabled[7];

static const char* enchant_names[7] = {
    "Copper Spear",
    "Diamond Spear",
    "Gold Spear",
    "Iron Spear",
    "Netherite Spear",
    "Stone Spear",
    "Wood Spear"
};

/* ===== BITMAPS ===== */

const unsigned char epd_bitmap_copper_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9c,0xfe,0x79,0xfe,0xf1,0xff,0x63,0xff,0x43,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_diamond_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9e,0xfe,0x7d,0xfe,0xf9,0xff,0x73,0xff,0x63,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_gold_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9e,0xfe,0x7d,0xfe,0xfd,0xff,0x7b,0xff,0x7b,0xfe,0x17,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_iron_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9c,0xfe,0x79,0xfe,0xf1,0xff,0x63,0xff,0x43,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_netherite_spear[] = {
0xff,0xf8,0xff,0xe0,0xff,0x80,0xfe,0x01,0xfe,0x01,0xff,0x03,0xff,0x03,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_stone_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9c,0xfe,0x39,0xfe,0x71,0xff,0x63,0xff,0x43,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_wood_spear[] = {
0xff,0xf8,0xff,0xe0,0xff,0x80,0xfe,0x01,0xfe,0x01,0xff,0x03,0xff,0x03,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char* epd_bitmap_allArray[7] = {
    epd_bitmap_copper_spear,
    epd_bitmap_diamond_spear,
    epd_bitmap_gold_spear,
    epd_bitmap_iron_spear,
    epd_bitmap_netherite_spear,
    epd_bitmap_stone_spear,
    epd_bitmap_wood_spear
};

static GLuint enchant_textures[7];

/* ========== BITMAP → TEXTURE ========== */

static GLuint createTexture(const unsigned char* bmp) {
    unsigned char rgba[16 * 16 * 4];
    memset(rgba, 0, sizeof(rgba));

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int byteIndex = (y * 16 + x) / 8;
            int bit = 7 - (x % 8);
            bool on = (bmp[byteIndex] >> bit) & 1;

            int i = (y * 16 + x) * 4;
            if (on) {
                rgba[i] = rgba[i+1] = rgba[i+2] = rgba[i+3] = 255;
            }
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

/* ========== UI ========== */

static void draw_menu() {
    ImGui::Begin("Enchant", nullptr, ImGuiWindowFlags_NoCollapse);

    for (int i = 0; i < 7; i++) {
        ImGui::Image((void*)(intptr_t)enchant_textures[i], ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::Checkbox(enchant_names[i], &enchant_enabled[i]);
    }

    ImGui::End();
}

/* ========== SETUP / RENDER ========== */

static void setup() {
    if (g_initialized) return;

    ImGui::CreateContext();
    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");

    for (int i = 0; i < 7; i++)
        enchant_textures[i] = createTexture(epd_bitmap_allArray[i]);

    g_initialized = true;
}

static void render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_width, g_height);
    ImGui::NewFrame();

    draw_menu();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/* ========== EGL HOOK ========== */

static EGLBoolean hook_eglswapbuffers(EGLDisplay dpy, EGLSurface surf) {
    if (!orig_eglswapbuffers)
        return EGL_FALSE;

    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    if (w > 500 && h > 500) {
        g_width = w;
        g_height = h;
        setup();
        render();
    }

    return orig_eglswapbuffers(dpy, surf);
}

static void* mainthread(void*) {
    sleep(3);

    GlossInit(true);
    GHandle hegl = GlossOpen("libEGL.so");
    if (!hegl) return nullptr;

    void* swap = (void*)GlossSymbol(hegl, "eglSwapBuffers", nullptr);
    if (!swap) return nullptr;

    GlossHook(swap, (void*)hook_eglswapbuffers, (void**)&orig_eglswapbuffers);
    return nullptr;
}

__attribute__((constructor))
void init() {
    pthread_t t;
    pthread_create(&t, nullptr, mainthread, nullptr);
}
