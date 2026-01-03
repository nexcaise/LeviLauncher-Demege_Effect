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

#include <vector>
#include <string>
#include <cstring>

static bool g_initialized = false;
static int g_width = 0, g_height = 0;
static EGLContext g_targetcontext = EGL_NO_CONTEXT;
static EGLSurface g_targetsurface = EGL_NO_SURFACE;

static EGLBoolean (*orig_eglswapbuffers)(EGLDisplay, EGLSurface) = nullptr;

/* =======================
   Enchant Data
   ======================= */

static bool enchant_enabled[7] = { false };

static const char* enchant_names[7] = {
    "Copper Spear",
    "Diamond Spear",
    "Gold Spear",
    "Iron Spear",
    "Netherite Spear",
    "Stone Spear",
    "Wood Spear"
};

/* ====== BITMAPS ====== */
const unsigned char epd_bitmap_copper_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9c,0xfe,0x79,0xfe,0xf1,0xff,0x63,0xff,0x43,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_gold_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9e,0xfe,0x7d,0xfe,0xfd,0xff,0x7b,0xff,0x7b,0xfe,0x17,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_wood_spear[] = {
0xff,0xf8,0xff,0xe0,0xff,0x80,0xfe,0x01,0xfe,0x01,0xff,0x03,0xff,0x03,0xfe,0x07,
0xfc,0x67,0xf8,0xff,0xf1,0xff,0xe3,0xff,0xc7,0xff,0x8f,0xff,0x1f,0xff,0x3f,0xff
};

const unsigned char epd_bitmap_diamond_spear[] = {
0xff,0xf8,0xff,0xe6,0xff,0x9e,0xfe,0x7d,0xfe,0xf9,0xff,0x73,0xff,0x63,0xfe,0x07,
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

const unsigned char* epd_bitmap_allArray[7] = {
    epd_bitmap_copper_spear,
    epd_bitmap_diamond_spear,
    epd_bitmap_gold_spear,
    epd_bitmap_iron_spear,
    epd_bitmap_netherite_spear,
    epd_bitmap_stone_spear,
    epd_bitmap_wood_spear
};

static GLuint enchant_textures[7] = { 0 };

/* =======================
   Bitmap → Texture
   ======================= */

static GLuint createTextureFrom1Bit(const unsigned char* bitmap) {
    unsigned char rgba[16 * 16 * 4];
    memset(rgba, 0, sizeof(rgba));

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int byteIndex = (y * 16 + x) / 8;
            int bitIndex = 7 - (x % 8);
            bool on = (bitmap[byteIndex] >> bitIndex) & 1;

            int idx = (y * 16 + x) * 4;
            if (on) {
                rgba[idx + 0] = 255;
                rgba[idx + 1] = 255;
                rgba[idx + 2] = 255;
                rgba[idx + 3] = 255;
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

/* =======================
   Menu
   ======================= */

static void draw_enchant_tab() {
    ImGui::SetWindowFontScale(1.3f);

    for (int i = 0; i < 7; i++) {
        ImGui::Image((void*)(intptr_t)enchant_textures[i], ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::Checkbox(enchant_names[i], &enchant_enabled[i]);
    }
}

static void drawmenu() {
    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("Enchant", nullptr, ImGuiWindowFlags_NoCollapse);
    draw_enchant_tab();
    ImGui::End();
}

/* =======================
   Setup / Render
   ======================= */

static void setup() {
    if (g_initialized) return;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");

    for (int i = 0; i < 7; i++)
        enchant_textures[i] = createTextureFrom1Bit(epd_bitmap_allArray[i]);

    g_initialized = true;
}

static void render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(g_width, g_height);
    ImGui::NewFrame();

    drawmenu();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/* =======================
   EGL Hook
   ======================= */

static EGLBoolean hook_eglswapbuffers(EGLDisplay dpy, EGLSurface surf) {
    if (!orig_eglswapbuffers) return EGL_FALSE;

    EGLContext ctx = eglGetCurrentContext();
    if (ctx == EGL_NO_CONTEXT)
        return orig_eglswapbuffers(dpy, surf);

    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);

    if (w < 500 || h < 500)
        return orig_eglswapbuffers(dpy, surf);

    g_width = w;
    g_height = h;

    setup();
    render();

    return orig_eglswapbuffers(dpy, surf);
}

static void* mainthread(void*) {
    sleep(3);
    GlossInit(true);
    GHandle hegl = GlossOpen("libEGL.so");
    void* swap = GlossSymbol(hegl, "eglSwapBuffers", nullptr);
    GlossHook(swap, (void*)hook_eglswapbuffers, (void**)&orig_eglswapbuffers);
    return nullptr;
}

__attribute__((constructor))
void init() {
    pthread_t t;
    pthread_create(&t, nullptr, mainthread, nullptr);
}
