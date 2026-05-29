// ==========================================================================
// V.I.C - Video Image Comparator v2.1
// Based on Video Comparator v2.0 (stable)
// + NEW: Cinema mode (F10) — hides UI to show only the player
// + NEW: ROI / Crop — draw a rectangle to limit metrics to a zone
// + NEW: Manual sync offset — for videos that don't start at the same moment
// + NEW: Window geometry saved (position, size, maximized state)
// + Reset ROI shortcut (R)
// ==========================================================================

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_syswm.h>

#include <GL/glew.h>
#pragma comment(lib, "opengl32.lib")

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <thread>
#include <atomic>
#include <future>
#include <mutex>
#include <iostream>
#include <set>
#include <map>
#include <deque>

namespace fs = std::filesystem;

// ───────────────────── GLOBALS ─────────────────────
enum class HWBackend { AUTO, DSHOW_DXVA2, CPU_FFMPEG }; 
enum ViewMode { VM_NONE, VM_SUB, VM_OVERLAY, VM_BLINK, VM_CLICK };
enum class SyncMode { FRAME_INDEX, TIMESTAMP };

HWBackend hw_backend = HWBackend::AUTO; 
bool use_original_fps = true;
bool linked = true;
bool zoom_linked = true;
bool metrics_on = true;
bool show_console = false; 
float playback_speed = 1.f;
ViewMode view_mode = VM_NONE;
bool playing = false;
bool vsync_enabled = true;
SyncMode sync_mode = SyncMode::TIMESTAMP;  // NEW: default timestamp sync
bool fullscreen = false;  // NEW: fullscreen state

// NEW: Recent files
static constexpr int MAX_RECENT = 8;
std::deque<std::pair<std::string, std::string>> recent_files;

// NEW: Cinema mode (hides UI to show only the player)
bool cinema_mode = false;

// NEW: Sync offset (manual frame offset for videos that don't start at the same moment)
int sync_offset_frames = 0;  // Added to frame2 when syncing from frame1

// NEW: ROI (Region of Interest) for metrics — normalized coords [0..1]
bool roi_enabled = false;
ImVec2 roi_min{0.25f, 0.25f};
ImVec2 roi_max{0.75f, 0.75f};
bool roi_editing = false;  // True when dragging to define a new ROI

// NEW: Window geometry (saved to INI)
int saved_win_x = 100, saved_win_y = 100;
int saved_win_w = 1600, saved_win_h = 900;
bool saved_win_maximized = false;

cv::VideoCapture cap1, cap2;
cv::VideoCapture cap1_seek, cap2_seek;
cv::Mat frame1, frame2;
int frame1_idx = 0, frame2_idx = 0;
double video_fps = 30.;
auto next_frame_time = std::chrono::steady_clock::now();

float wipe_pos = .5f;
float zoom_l = 1.f, zoom_r = 1.f;
ImVec2 center_l { .5f, .5f }, center_r { .5f, .5f };
float effect_intensity = .5f;

GLuint tex1 = 0, tex2 = 0, tex_sub = 0;
int tex1_w = 0, tex1_h = 0, tex2_w = 0, tex2_h = 0;
static GLuint tex_about = 0; static int about_w=0, about_h=0;
static int tex_sub_w=0, tex_sub_h=0;

double mse=0, psnr=0, ssim=0;
std::vector<float> psnrHist, ssimHist;

std::string path1;
std::string path2;

// Chemins "Safe" (Alias temporaires)
std::string path1_safe;
std::string path2_safe;

bool loaded = false;
bool frame1_fresh = false, frame2_fresh = false;

// Anti-Rebond Seek
int last_seek_req_1 = -1; 
int last_seek_req_2 = -1;
auto last_seek_time = std::chrono::steady_clock::now();

struct VidInfo{
    int w=0,h=0; float fps=0; std::string fourcc; std::string codecTag; std::string codecPretty; double br=0;
    int total_frames = 0;
};
VidInfo info1, info2;

static bool show_rebinding = false;
static bool about_open = false;
static bool need_close_load_popup = false;

static bool isImage1 = false, isImage2 = false;
static cv::Mat img1_static, img2_static;

std::atomic<bool> is_loading_async{false};
// FIX: Thread-safe loading message using atomic pointer to string literals
static std::atomic<const char*> loading_msg_atomic{""};
std::future<void> loader_future;

struct LoadResult {
    bool s1=false, s2=false;
    VidInfo i1, i2;
    cv::VideoCapture c1, c2, cs1, cs2;
    cv::Mat m1, m2; 
    bool img1=false, img2=false;
    cv::Mat sImg1, sImg2;
    std::string p1_safe_out, p2_safe_out;
};
LoadResult g_res;

// ───────────────────── Shortcuts ─────────────────────
struct Shortcut {
    const char* name; ImGuiKey key; bool withCtrl=false, withShift=false, withAlt=false;
    ImGuiKey defaultKey; bool defaultCtrl, defaultShift, defaultAlt;
};
static std::vector<Shortcut> g_keys = {
    {"Play/Pause", ImGuiKey_Space, false,false,false, ImGuiKey_Space,false,false,false},
    {"Frame +1", ImGuiKey_Period, false,false,false, ImGuiKey_Period,false,false,false},
    {"Frame -1", ImGuiKey_Comma, false,false,false, ImGuiKey_Comma,false,false,false},
    {"Seek +10", ImGuiKey_RightArrow, true,false,false, ImGuiKey_RightArrow,true,false,false},
    {"Seek -10", ImGuiKey_LeftArrow,  true,false,false, ImGuiKey_LeftArrow,true,false,false},
    {"Home (0)", ImGuiKey_Home, false,false,false, ImGuiKey_Home,false,false,false},
    {"End (last)", ImGuiKey_End, false,false,false, ImGuiKey_End,false,false,false},
    {"+ Speed", ImGuiKey_Equal, false,false,false, ImGuiKey_Equal,false,false,false},
    {"- Speed", ImGuiKey_Minus, false,false,false, ImGuiKey_Minus,false,false,false},
    {"Mode: Sub", ImGuiKey_1, false,false,false, ImGuiKey_1,false,false,false},
    {"Mode: Overlay", ImGuiKey_2, false,false,false, ImGuiKey_2,false,false,false},
    {"Mode: Blink", ImGuiKey_3, false,false,false, ImGuiKey_3,false,false,false},
    {"Mode: Click", ImGuiKey_4, false,false,false, ImGuiKey_4,false,false,false},
    {"Toggle Metrics", ImGuiKey_M, false,false,false, ImGuiKey_M,false,false,false},
    {"Toggle Link", ImGuiKey_L, false,false,false, ImGuiKey_L,false,false,false},
    {"Toggle Zoom Link", ImGuiKey_Z, false,false,false, ImGuiKey_Z,false,false,false},
    {"Fullscreen", ImGuiKey_F11, false,false,false, ImGuiKey_F11,false,false,false},
    {"Cinema Mode", ImGuiKey_F10, false,false,false, ImGuiKey_F10,false,false,false},
    {"Reset ROI", ImGuiKey_R, false,false,false, ImGuiKey_R,false,false,false},
};
static bool matchShortcut(const Shortcut& s, ImGuiIO& io) {
    bool mods = (!s.withCtrl || io.KeyCtrl) && (!s.withShift || io.KeyShift) && (!s.withAlt || io.KeyAlt);
    return mods && ImGui::IsKeyPressed(s.key, false);
}
static void resetShortcuts(){ for(auto& s:g_keys){ s.key=s.defaultKey; s.withCtrl=s.defaultCtrl; s.withShift=s.defaultShift; s.withAlt=s.defaultAlt; }}

// ───────── Timestamp Sync Helpers ─────────
static double frameToTime(int frameIdx, const VidInfo& info) {
    double fps = info.fps > 0 ? info.fps : 30.0;
    return frameIdx / fps;
}
static int timeToFrame(double timeSec, const VidInfo& info) {
    double fps = info.fps > 0 ? info.fps : 30.0;
    int f = (int)std::round(timeSec * fps);
    int maxF = std::max(0, info.total_frames - 1);
    return std::clamp(f, 0, maxF);
}
static int syncedFrame2FromFrame1(int f1idx) {
    int maxF = std::max(0, info2.total_frames - 1);
    int base;
    if(sync_mode == SyncMode::FRAME_INDEX)
        base = f1idx;
    else
        base = timeToFrame(frameToTime(f1idx, info1), info2);
    return std::clamp(base + sync_offset_frames, 0, maxF);
}
static int syncedFrame1FromFrame2(int f2idx) {
    int maxF = std::max(0, info1.total_frames - 1);
    int base;
    if(sync_mode == SyncMode::FRAME_INDEX)
        base = f2idx;
    else
        base = timeToFrame(frameToTime(f2idx, info2), info1);
    return std::clamp(base - sync_offset_frames, 0, maxF);
}

// ───────── Recent Files ─────────
static void addRecent(const std::string& p1, const std::string& p2) {
    if(p1.empty() || p2.empty()) return;
    auto pair = std::make_pair(p1, p2);
    recent_files.erase(std::remove(recent_files.begin(), recent_files.end(), pair), recent_files.end());
    recent_files.push_front(pair);
    while((int)recent_files.size() > MAX_RECENT) recent_files.pop_back();
}

// ───────── Fullscreen ─────────
static void toggleFullscreen(SDL_Window* win) {
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

// NEW: Cinema mode hides UI chrome to show only the player
static void toggleCinema() {
    cinema_mode = !cinema_mode;
}

// NEW: Reset ROI to sensible default
static void resetRoi() {
    roi_enabled = false;
    roi_min = ImVec2(0.25f, 0.25f);
    roi_max = ImVec2(0.75f, 0.75f);
}

// NEW: Apply ROI to a Mat (returns cropped Mat in pixel coords)
static cv::Mat applyRoi(const cv::Mat& src) {
    if(!roi_enabled || src.empty()) return src;
    int x = std::clamp((int)(roi_min.x * src.cols), 0, src.cols-1);
    int y = std::clamp((int)(roi_min.y * src.rows), 0, src.rows-1);
    int w = std::clamp((int)((roi_max.x - roi_min.x) * src.cols), 1, src.cols - x);
    int h = std::clamp((int)((roi_max.y - roi_min.y) * src.rows), 1, src.rows - y);
    return src(cv::Rect(x, y, w, h)).clone();
}

// ───────── Utilitaires ─────────
static inline void SetupGLPackingOnce() { static bool done=false; if(!done){ glPixelStorei(GL_UNPACK_ALIGNMENT,1); done=true; } }

static std::string filenameOnly(const std::string& p) { 
    if(p.empty()) return {}; 
    std::string fname = fs::path(std::u8string(p.begin(),p.end())).filename().string();
    int wlen = MultiByteToWideChar(CP_ACP, 0, fname.c_str(), -1, nullptr, 0);
    if(wlen > 0) {
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(CP_ACP, 0, fname.c_str(), -1, &wstr[0], wlen);
        int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if(ulen > 0) {
            std::string ustr(ulen, 0);
            WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &ustr[0], ulen, nullptr, nullptr);
            while(!ustr.empty() && ustr.back()==0) ustr.pop_back();
            return ustr;
        }
    }
    return fname;
}

static inline void Tip(const char* text){ if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", text); }

static void ToggleConsole(bool show) {
    if(show) { AllocConsole(); freopen_s((FILE**)stdout, "CONOUT$", "w", stdout); }
    else FreeConsole();
}

// Convertit un chemin UTF-8 en ShortPath (8.3) ANSI
static std::string toShortPath(const std::string& utf8Path) {
    if(utf8Path.empty()) return "";
    fs::path p = std::u8string(utf8Path.begin(), utf8Path.end());
    if (!fs::exists(p)) return utf8Path;
    p = fs::absolute(p);
    std::wstring wpath = p.wstring();
    DWORD shortLen = GetShortPathNameW(wpath.c_str(), nullptr, 0);
    if(shortLen == 0) return utf8Path;
    std::wstring wshort(shortLen, 0);
    GetShortPathNameW(wpath.c_str(), &wshort[0], shortLen);
    int alen = WideCharToMultiByte(CP_ACP, 0, wshort.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if(alen <= 0) return utf8Path;
    std::string shortPath(alen, 0);
    WideCharToMultiByte(CP_ACP, 0, wshort.c_str(), -1, &shortPath[0], alen, nullptr, nullptr);
    while(!shortPath.empty() && shortPath.back() == 0) shortPath.pop_back();
    return shortPath;
}

// ─── FIX DOSSIER CACHÉ ───
static std::string createSafeAlias(const std::string& originalPath, int index) {
    if(originalPath.empty()) return "";
    fs::path src = std::u8string(originalPath.begin(), originalPath.end());
    if(!fs::exists(src)) return "";

    fs::path dir = src.parent_path();
    fs::path stagingDir = dir / ".vc_temp"; 
    
    std::error_code ec;
    if(!fs::exists(stagingDir)) {
        fs::create_directory(stagingDir, ec);
        SetFileAttributesW(stagingDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
    }

    std::string ext = src.extension().string();
    if(ext.empty()) ext = ".mkv";
    
    fs::path dst = stagingDir / ("safe_" + std::to_string(index) + ext);

    if(fs::exists(dst)) fs::remove(dst, ec);

    fs::create_hard_link(src, dst, ec);
    if(ec) fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    
    return dst.string();
}

// v1.4 approach restored: fire-and-forget CMD process that runs AFTER the app exits
// This works because the detached process waits for handles to be released naturally
static void RunSelfDestructCmd(const std::string& folderPathUTF8) {
    if(folderPathUTF8.empty()) return;
    
    std::string safeDir = toShortPath(folderPathUTF8);
    if(safeDir.empty()) return;

    // Ping 127.0.0.1 -n 3 = ~2 second delay, then force-remove the folder
    std::string cmd = "/C ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"" + safeDir + "\"";

    ShellExecuteA(NULL, "open", "cmd.exe", cmd.c_str(), NULL, SW_HIDE);
}

static void cleanupAliases() {
    cap1.release(); cap1_seek.release();
    cap2.release(); cap2_seek.release();
    
    std::set<std::string> foldersToRemove;

    auto check = [&](const std::string& p_str) {
        if(p_str.empty()) return;
        fs::path p = std::u8string(p_str.begin(), p_str.end());
        fs::path parent = p.parent_path();
        if (parent.filename() == ".vc_temp") {
            std::u8string u8loc = fs::absolute(parent).u8string();
            foldersToRemove.insert(std::string(u8loc.begin(), u8loc.end()));
        }
    };

    if(!path1_safe.empty()) check(path1_safe);
    if(!path2_safe.empty()) check(path2_safe);

    for(const auto& f : foldersToRemove) {
        RunSelfDestructCmd(f);
    }
}

static std::string fourccStr(int f){
    char a=f&255,b=(f>>8)&255,c=(f>>16)&255,d=(f>>24)&255;
    std::string s{a,b,c,d};
    for(char&ch:s) if(!isprint((unsigned char)ch)) ch=' ';
    return s;
}
static std::string lower(std::string s){ for(auto&c:s)c=(char)tolower((unsigned char)c); return s; }
static std::string prettyFromTag(const std::string& lc){
    if(lc=="h264"||lc=="avc1") return "H.264";
    if(lc=="hevc"||lc=="h265"||lc=="hvc1") return "H.265/HEVC";
    if(lc=="av01") return "AV1";
    if(lc=="vp09"||lc=="vp9 ") return "VP9";
    if(lc=="mp4v"||lc=="mpeg") return "MPEG-4";
    return lc;
}
static void inferCodecFromFilename(const std::string& path, VidInfo& info){
    std::string full = lower(path);
    auto set = [&](const char* tag){ info.codecTag = tag; info.codecPretty = prettyFromTag(info.codecTag); };
    if(full.find("av1")!=std::string::npos) { set("av01"); return; }
    if(full.find("hevc")!=std::string::npos || full.find("x265")!=std::string::npos) { set("hevc"); return; }
    if(full.find("h264")!=std::string::npos || full.find("x264")!=std::string::npos || full.find("avc")!=std::string::npos) { set("h264"); return; }
    if(full.find("vp9")!=std::string::npos) { set("vp09"); return; }

    if(!info.fourcc.empty()) {
        std::string fcc = lower(info.fourcc);
        if(fcc == "hevc" || fcc == "hvc1") set("hevc");
        else if(fcc == "avc1" || fcc == "h264") set("h264");
        else { info.codecTag = info.fourcc; info.codecPretty = info.fourcc; }
    } else { info.codecPretty = "Unknown"; }
}

static bool isImageExt(const std::string& p){
    std::string ext = lower(fs::path(std::u8string(p.begin(),p.end())).extension().string());
    return (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".tif"||ext==".tiff");
}

static std::filesystem::path exeDir();

// FIX: Settings INI now uses key=value format with backward compatibility
static void saveIni(){
    std::ofstream f((exeDir() / "settings.ini").string());
    if(!f) return;
    f << "hw_backend=" << int(hw_backend) << "\n";
    f << "use_original_fps=" << use_original_fps << "\n";
    f << "linked=" << linked << "\n";
    f << "metrics_on=" << metrics_on << "\n";
    f << "playback_speed=" << playback_speed << "\n";
    f << "vsync_enabled=" << vsync_enabled << "\n";
    f << "zoom_linked=" << zoom_linked << "\n";
    f << "show_console=" << show_console << "\n";
    f << "sync_mode=" << int(sync_mode) << "\n";
    f << "sync_offset_frames=" << sync_offset_frames << "\n";
    f << "win_x=" << saved_win_x << "\n";
    f << "win_y=" << saved_win_y << "\n";
    f << "win_w=" << saved_win_w << "\n";
    f << "win_h=" << saved_win_h << "\n";
    f << "win_maximized=" << (saved_win_maximized ? 1 : 0) << "\n";
    int i = 0;
    for(const auto& [p1, p2] : recent_files) {
        f << "recent" << i << "_l=" << p1 << "\n";
        f << "recent" << i << "_r=" << p2 << "\n";
        ++i;
    }
}
static void loadIni(){
    std::ifstream f((exeDir() / "settings.ini").string());
    if(!f) return;
    
    // Try key=value format first
    std::string firstLine;
    std::getline(f, firstLine);
    f.seekg(0);
    
    if(firstLine.find('=') != std::string::npos) {
        // New key=value format
        std::map<std::string, std::string> kv;
        std::string line;
        while(std::getline(f, line)) {
            auto eq = line.find('=');
            if(eq != std::string::npos) {
                kv[line.substr(0, eq)] = line.substr(eq+1);
            }
        }
        auto getInt = [&](const char* key, int def) -> int {
            auto it = kv.find(key); return it != kv.end() ? std::stoi(it->second) : def;
        };
        auto getFloat = [&](const char* key, float def) -> float {
            auto it = kv.find(key); return it != kv.end() ? std::stof(it->second) : def;
        };
        hw_backend = HWBackend(getInt("hw_backend", 0));
        use_original_fps = !!getInt("use_original_fps", 1);
        linked = !!getInt("linked", 1);
        metrics_on = !!getInt("metrics_on", 1);
        playback_speed = getFloat("playback_speed", 1.f);
        vsync_enabled = !!getInt("vsync_enabled", 1);
        zoom_linked = !!getInt("zoom_linked", 1);
        show_console = !!getInt("show_console", 0);
        sync_mode = SyncMode(getInt("sync_mode", int(SyncMode::TIMESTAMP)));
        sync_offset_frames = getInt("sync_offset_frames", 0);
        saved_win_x = getInt("win_x", 100);
        saved_win_y = getInt("win_y", 100);
        saved_win_w = getInt("win_w", 1600);
        saved_win_h = getInt("win_h", 900);
        saved_win_maximized = !!getInt("win_maximized", 0);
        // Load recent files
        auto getStr = [&](const std::string& key) -> std::string {
            auto it = kv.find(key); return it != kv.end() ? it->second : "";
        };
        recent_files.clear();
        for(int i = 0; i < MAX_RECENT; ++i) {
            std::string pl = getStr("recent" + std::to_string(i) + "_l");
            std::string pr = getStr("recent" + std::to_string(i) + "_r");
            if(!pl.empty() && !pr.empty()) recent_files.push_back({pl, pr});
        }
    } else {
        // Legacy positional format — backward compat
        f.seekg(0);
        int hw,u,l,m,z=1,sc=0; float sp; int vs;
        if(f>>hw>>u>>l>>m>>sp>>vs){
            hw_backend=HWBackend(hw);use_original_fps=u;linked=l;metrics_on=m;playback_speed=sp;vsync_enabled=!!vs;
            if(f>>z) zoom_linked=!!z;
            if(f>>sc) show_console=!!sc;
        }
    }
}

static std::filesystem::path exeDir() {
    wchar_t wpath[MAX_PATH]{0};
    GetModuleFileNameW(nullptr, wpath, MAX_PATH);
    return std::filesystem::path(wpath).parent_path();
}
static std::string findAboutImage() {
    const char* names[] = { "assets/backroom.jpg", "assets/background.jpg", "res/backroom.jpg", "backroom.jpg" };
    fs::path base = exeDir();
    for(int i=0; i<4; ++i) {
        for(auto& n : names) {
            fs::path p = base / n;
            if(fs::exists(p)) return p.string();
        }
        if(base.has_parent_path()) base = base.parent_path(); else break;
    }
    return {};
}

static bool fileDlg(std::string& outPath){
    wchar_t wbuf[32768] = L"";
    OPENFILENAMEW o{sizeof(o)};
    o.lpstrFile=wbuf; o.nMaxFile=32768;
    o.lpstrFilter=L"Médias (vidéo+image)\0*.mp4;*.mkv;*.avi;*.mov;*.webm;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0Tous\0*.*\0";
    o.Flags=OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(!GetOpenFileNameW(&o)) return false;
    int size = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &s[0], size, nullptr, nullptr);
    while(!s.empty() && s.back()=='\0') s.pop_back();
    outPath = s;
    return true;
}

static void ensureTex(GLuint& t, int w, int h){
    if(!t){ glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,nullptr);
        glBindTexture(GL_TEXTURE_2D,0);
    }
}
static void uploadMat(GLuint& t, int& tw, int& th, const cv::Mat& bgr){
    if(bgr.empty()) return;
    cv::Mat rgb; cv::cvtColor(bgr,rgb,cv::COLOR_BGR2RGB);
    SetupGLPackingOnce();
    if(!t || rgb.cols!=tw || rgb.rows!=th){
        tw = rgb.cols; th = rgb.rows;
        if(t) glDeleteTextures(1, &t);
        t = 0;
        ensureTex(t, tw, th);
        glBindTexture(GL_TEXTURE_2D,t);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,tw,th,0,GL_RGB,GL_UNSIGNED_BYTE,rgb.data);
    } else {
        glBindTexture(GL_TEXTURE_2D,t);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,tw,th,GL_RGB,GL_UNSIGNED_BYTE,rgb.data);
    }
    glBindTexture(GL_TEXTURE_2D,0);
}
static void uploadImageTo(GLuint& t, int& tw, int& th, const cv::Mat& bgr){
    if(bgr.empty()) return;
    cv::Mat rgb; cv::cvtColor(bgr,rgb,cv::COLOR_BGR2RGB);
    SetupGLPackingOnce();
    if(t) glDeleteTextures(1, &t);
    t = 0;
    ensureTex(t, rgb.cols, rgb.rows);
    tw = rgb.cols; th = rgb.rows;
    glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,tw,th,0,GL_RGB,GL_UNSIGNED_BYTE,rgb.data);
    glBindTexture(GL_TEXTURE_2D,0);
}

static void fillCodec(VidInfo& info, const cv::VideoCapture& cap){
    int fcc = int(cap.get(cv::CAP_PROP_FOURCC));
    info.fourcc = fourccStr(fcc);
    std::string lc = lower(info.fourcc);
    if(lc.find_first_not_of(' ') != std::string::npos){
        info.codecTag = (lc=="avc1")?"h264":(lc=="hvc1"?"hevc":lc);
        info.codecPretty = prettyFromTag(info.codecTag);
    }
}

static bool openRobust(const std::string& p, cv::VideoCapture& cap) {
    cap.release();
    if (hw_backend == HWBackend::AUTO || hw_backend == HWBackend::CPU_FFMPEG) {
        try { cap.open(p, cv::CAP_FFMPEG); if(cap.isOpened()) return true; } catch(...) {}
    }
    if (hw_backend == HWBackend::DSHOW_DXVA2 || hw_backend == HWBackend::AUTO) {
        try { cap.open(p, cv::CAP_DSHOW); if(cap.isOpened()) return true; } catch(...) {}
    }
    cap.open(p, cv::CAP_ANY);
    return cap.isOpened();
}

static bool openAny(const std::string& safePath, const std::string& originalPath, cv::VideoCapture& cap, VidInfo& info, bool& isImg, cv::Mat& staticImg){
    isImg = false; staticImg.release();
    if(!fs::exists(std::u8string(originalPath.begin(), originalPath.end()))) return false;

    if(isImageExt(originalPath)){
        isImg = true;
        staticImg = cv::imread(safePath, cv::IMREAD_COLOR);
        if(staticImg.empty()) return false;
        info.w = staticImg.cols; info.h = staticImg.rows;
        info.fps = 0.f; info.fourcc="IMG "; info.codecTag="image"; info.codecPretty="Image";
        info.br = (double)fs::file_size(std::u8string(originalPath.begin(), originalPath.end()))*8.0/1024/1024;
        info.total_frames = 1;
        return true;
    }

    if(!openRobust(safePath, cap)) return false;

    info.w=int(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    info.h=int(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    info.fps=float(cap.get(cv::CAP_PROP_FPS));
    info.total_frames = int(cap.get(cv::CAP_PROP_FRAME_COUNT));
    
    fillCodec(info, cap);
    if(info.codecPretty.empty() || info.codecTag.empty() || info.fourcc.find_first_not_of(' ') == std::string::npos)
        inferCodecFromFilename(originalPath, info);

    double frames = info.total_frames;
    double fps_ = std::max(info.fps, 1.f);
    double dur = (frames>0 ? frames/fps_ : 0.0);
    info.br = (dur>0) ? ((double)fs::file_size(std::u8string(originalPath.begin(), originalPath.end()))*8.0/1000000.0)/dur : 0.0;

    return true;
}

static void AsyncLoadWorker(std::string p1, std::string p2) {
    g_res = LoadResult();
    
    if(!p1.empty()){
        loading_msg_atomic.store("Analyse Media 1...");
        g_res.p1_safe_out = createSafeAlias(p1, 1);
        std::string t1 = g_res.p1_safe_out.empty() ? p1 : g_res.p1_safe_out;

        g_res.s1 = openAny(t1, p1, g_res.c1, g_res.i1, g_res.img1, g_res.sImg1);
        if(g_res.s1 && !g_res.img1) { if(!g_res.c1.read(g_res.m1)) {} }
        else if(g_res.img1) { g_res.m1 = g_res.sImg1; }
        
        if(g_res.s1 && !g_res.img1) openRobust(t1, g_res.cs1);
    }

    if(!p2.empty()){
        loading_msg_atomic.store("Analyse Media 2...");
        g_res.p2_safe_out = createSafeAlias(p2, 2);
        std::string t2 = g_res.p2_safe_out.empty() ? p2 : g_res.p2_safe_out;

        g_res.s2 = openAny(t2, p2, g_res.c2, g_res.i2, g_res.img2, g_res.sImg2);
        if(g_res.s2 && !g_res.img2) { g_res.c2.read(g_res.m2); }
        else if(g_res.img2) { g_res.m2 = g_res.sImg2; }

        if(g_res.s2 && !g_res.img2) openRobust(t2, g_res.cs2);
    }
    loading_msg_atomic.store("Finalisation...");
}

// Light cleanup for reload: only release captures (not mats/frames)
static void releaseCapsOnly() {
    cap1.release(); cap1_seek.release();
    cap2.release(); cap2_seek.release();
}

static void StartLoading(const std::string& p1, const std::string& p2) {
    if(is_loading_async) return;
    releaseCapsOnly();  // Don't destroy frames/mats during reload
    loaded = false;
    is_loading_async = true;
    loader_future = std::async(std::launch::async, AsyncLoadWorker, p1, p2);
}

// FIX: Resize frames to common size before metrics to prevent crash on mismatched resolutions
static void resizeToCommon(const cv::Mat& a, const cv::Mat& b, cv::Mat& outA, cv::Mat& outB) {
    if(a.empty() || b.empty()) { outA = a; outB = b; return; }
    if(a.cols == b.cols && a.rows == b.rows) {
        outA = a; outB = b; return;
    }
    // Use the smaller resolution as target to avoid upscaling artifacts
    int tw = std::min(a.cols, b.cols);
    int th = std::min(a.rows, b.rows);
    cv::resize(a, outA, cv::Size(tw, th), 0, 0, cv::INTER_AREA);
    cv::resize(b, outB, cv::Size(tw, th), 0, 0, cv::INTER_AREA);
}

static double SSIM(const cv::Mat& a, const cv::Mat& b) {
    const double C1 = 6.5025, C2 = 58.5225;
    cv::Mat f1, f2; 
    a.convertTo(f1, CV_32F); 
    b.convertTo(f2, CV_32F);
    cv::Mat m1, m2; 
    cv::GaussianBlur(f1, m1, {11, 11}, 1.5);
    cv::GaussianBlur(f2, m2, {11, 11}, 1.5);
    cv::Mat m1_2 = m1.mul(m1);
    cv::Mat m2_2 = m2.mul(m2);
    cv::Mat m1m2 = m1.mul(m2);
    cv::Mat s1, s2, s12;
    cv::GaussianBlur(f1.mul(f1), s1, {11, 11}, 1.5); s1 -= m1_2;
    cv::GaussianBlur(f2.mul(f2), s2, {11, 11}, 1.5); s2 -= m2_2;
    cv::GaussianBlur(f1.mul(f2), s12, {11, 11}, 1.5); s12 -= m1m2;
    cv::Mat num = (2 * m1m2 + C1).mul(2 * s12 + C2);
    cv::Mat den = (m1_2 + m2_2 + C1).mul(s1 + s2 + C2);
    cv::Mat s;
    cv::divide(num, den, s);
    return cv::mean(s)[0];
}

// FIX: makeSubTex now handles mismatched frame sizes
static void makeSubTex(const cv::Mat&a,const cv::Mat&b){
    if(a.empty()||b.empty()) return;
    cv::Mat ra, rb;
    resizeToCommon(a, b, ra, rb);
    // Apply ROI if enabled
    if(roi_enabled) {
        ra = applyRoi(ra);
        rb = applyRoi(rb);
        if(ra.empty() || rb.empty()) return;
    }
    cv::Mat d; cv::absdiff(ra,rb,d);
    cv::Mat g; cv::cvtColor(d,g,cv::COLOR_BGR2GRAY);
    cv::Mat heat; cv::applyColorMap(g,heat,cv::COLORMAP_JET);
    uploadMat(tex_sub, tex_sub_w, tex_sub_h, heat);
}

// FIX: computeMetrics now handles mismatched frame sizes + ROI support
static void computeMetrics(){
    if(!metrics_on) return;
    if(frame1.empty()||frame2.empty()) return;
    
    cv::Mat common1, common2;
    resizeToCommon(frame1, frame2, common1, common2);
    
    // Apply ROI if enabled — crop BEFORE downscaling
    if(roi_enabled) {
        common1 = applyRoi(common1);
        common2 = applyRoi(common2);
        if(common1.empty() || common2.empty()) return;
    }
    
    cv::Mat r1,r2; 
    cv::resize(common1,r1,{},.25,.25); 
    cv::resize(common2,r2,{},.25,.25);
    
    cv::Mat diff; cv::absdiff(r1,r2,diff); diff.convertTo(diff,CV_32F);
    cv::Mat sq = diff.mul(diff); cv::Scalar m = cv::mean(sq);
    mse = (m[0]+m[1]+m[2])/3.0; psnr = (mse>1e-10) ? 10.0*log10((255.0*255.0)/mse) : 99.0;
    ssim = SSIM(r1,r2);
    psnrHist.push_back((float)psnr); ssimHist.push_back((float)ssim);
    if(psnrHist.size()>200){ psnrHist.erase(psnrHist.begin()); ssimHist.erase(ssimHist.begin()); }
}
static void readCPUAt(cv::VideoCapture& capCPU, int idx, cv::Mat& out){
    if(!capCPU.isOpened()) return;
    try {
        capCPU.set(cv::CAP_PROP_POS_FRAMES, idx);
        capCPU.read(out);
    } catch(...) {}
}

// Helper: get max frame index for a given side
static int getMaxFrame(bool isImg, const VidInfo& info) {
    if(isImg) return 0;
    return std::max(0, info.total_frames - 1);
}

static void updateFrame(){
    static int prev1=-1, prev2=-1;
    bool needs_metric_update = false;

    // FIX: Clamp frame indices to valid range for each video independently
    int max1 = getMaxFrame(isImage1, info1);
    int max2 = getMaxFrame(isImage2, info2);
    frame1_idx = std::clamp(frame1_idx, 0, max1);
    frame2_idx = std::clamp(frame2_idx, 0, max2);

    if(isImage1 && !img1_static.empty()){
        if(frame1.empty()){ frame1 = img1_static; frame1_fresh=true; }
        frame1_idx = 0;
    }
    if(isImage2 && !img2_static.empty()){
        if(frame2.empty()){ frame2 = img2_static; frame2_fresh=true; }
        frame2_idx = linked ? syncedFrame2FromFrame1(frame1_idx) : 0;
    }

    if(playing){
        double tgt = (use_original_fps && info1.fps>0 ? info1.fps : 30.0) * playback_speed;
        auto step = std::chrono::duration<double>(1.0 / std::max(1e-6, tgt));
        for(int guard=0; guard<2; ++guard){ 
            auto now = std::chrono::steady_clock::now();
            if(now < next_frame_time) break;
            next_frame_time += std::chrono::duration_cast<std::chrono::nanoseconds>(step);

            if(!isImage1 && cap1.isOpened()){ 
                cv::Mat tmp;
                if(cap1.read(tmp) && !tmp.empty()) {
                    frame1 = tmp; 
                    ++frame1_idx;
                    frame1_fresh = true;
                }
            }
            if(!isImage2 && cap2.isOpened()){
                if(linked) {
                    int targetIdx = syncedFrame2FromFrame1(frame1_idx);
                    int current = (int)cap2.get(cv::CAP_PROP_POS_FRAMES);
                    if(abs(current - targetIdx) > 5) cap2.set(cv::CAP_PROP_POS_FRAMES, targetIdx);
                    cv::Mat tmp;
                    if(cap2.read(tmp) && !tmp.empty()) { frame2 = tmp; frame2_fresh = true; }
                }
                else { 
                    cv::Mat tmp;
                    if(cap2.read(tmp) && !tmp.empty()) {
                        frame2 = tmp; 
                        frame2_idx = (int)cap2.get(cv::CAP_PROP_POS_FRAMES) - 1;
                        frame2_fresh = true;
                    }
                }
            }
        }

        if(linked) frame2_idx = syncedFrame2FromFrame1(frame1_idx);

        // FIX: Loop handling respects both video lengths
        if(!isImage1 && cap1.isOpened()){
            if(frame1_idx >= info1.total_frames && info1.total_frames > 0){
                frame1_idx=0;
                frame2_idx=0;
                cap1.set(cv::CAP_PROP_POS_FRAMES,0);
                if(!isImage2 && cap2.isOpened()) cap2.set(cv::CAP_PROP_POS_FRAMES,0);
                cap1.read(frame1); if(!isImage2 && cap2.isOpened()) cap2.read(frame2);
                frame1_fresh = frame2_fresh = true;
            }
        }
    } else {
        auto now = std::chrono::steady_clock::now();
        bool do_seek = (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_seek_time).count() > 50);

        if(do_seek && (frame1_idx!=prev1 || frame2_idx!=prev2)){
            if (frame1_idx != prev1) last_seek_req_1 = frame1_idx;
            if (frame2_idx != prev2) last_seek_req_2 = frame2_idx;
            prev1=frame1_idx; prev2=frame2_idx;
            last_seek_time = now; 

            if(!isImage1 && cap1_seek.isOpened()){
                readCPUAt(cap1_seek, frame1_idx, frame1);
                frame1_fresh = true;
            }
            int idx2 = linked ? syncedFrame2FromFrame1(frame1_idx) : frame2_idx;
            if(!isImage2 && cap2_seek.isOpened()){
                readCPUAt(cap2_seek, idx2, frame2);
                frame2_fresh = true;
            }
            if(linked) frame2_idx = syncedFrame2FromFrame1(frame1_idx);
        }
    }

    if(frame1_fresh && !frame1.empty()) { uploadMat(tex1, tex1_w, tex1_h, frame1); frame1_fresh=false; needs_metric_update=true; }
    if(frame2_fresh && !frame2.empty()) { uploadMat(tex2, tex2_w, tex2_h, frame2); frame2_fresh=false; needs_metric_update=true; }

    if((metrics_on || view_mode==VM_SUB) && needs_metric_update){ 
        computeMetrics(); 
        if(view_mode==VM_SUB) makeSubTex(frame1,frame2); 
    }
}

static void uiStyle(){
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 10.0f;
    s.FrameRounding  = 8.0f;
    s.GrabRounding   = 8.0f;
    s.ScrollbarRounding = 9.0f;
    s.FramePadding   = ImVec2(12, 8);
    s.ItemSpacing    = ImVec2(12, 10);
    s.HoverDelayNormal = 1.20f;
    s.HoverDelayShort  = 0.30f;
    ImVec4 bg = ImVec4(0.07f, 0.08f, 0.09f, 1.0f);
    ImVec4 p1 = ImVec4(0.12f, 0.38f, 0.65f, 1.0f);
    ImVec4 p2 = ImVec4(0.18f, 0.48f, 0.80f, 1.0f);
    ImVec4 p3 = ImVec4(0.10f, 0.42f, 0.72f, 1.0f);
    s.Colors[ImGuiCol_WindowBg]         = bg;
    s.Colors[ImGuiCol_PopupBg]          = ImVec4(0.10f,0.11f,0.12f,1.0f);
    s.Colors[ImGuiCol_FrameBg]          = ImVec4(0.12f,0.13f,0.15f,1.0f);
    s.Colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.18f,0.20f,0.24f,1.0f);
    s.Colors[ImGuiCol_FrameBgActive]    = ImVec4(0.16f,0.18f,0.22f,1.0f);
    s.Colors[ImGuiCol_Button]           = p1;
    s.Colors[ImGuiCol_ButtonHovered]    = p2;
    s.Colors[ImGuiCol_ButtonActive]     = p3;
    s.Colors[ImGuiCol_SliderGrab]       = p2;
    s.Colors[ImGuiCol_SliderGrabActive] = p3;
}

static float widthForDigits(int tot){
    int d = (tot<=0)?3: (int)std::floor(std::log10((double)tot))+1;
    d = std::max(d, 4);
    return  (float)(d*11 + 36);
}
static bool FrameRowHalf(const char* id, int& idx, int tot, bool alignRight, float sidePad = 20.0f){
    ImGui::PushID(id);
    bool changed = false;
    int  tmp = idx;

    ImGui::Dummy(ImVec2(sidePad, 0)); ImGui::SameLine(0.0f, 0.0f);

    float fullW   = ImGui::GetContentRegionAvail().x - sidePad;
    float inputW  = widthForDigits(tot);
    float btnW    = 28.0f;
    float pad     = 6.0f;
    float gap     = 10.0f;
    float sliderW = std::max(120.0f, fullW - (inputW + 2*btnW + pad*2 + gap));

    auto drawButtonsPlusMinus = [&](bool reverseOrder){
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        if (!reverseOrder) {
            if (ImGui::Button("-", ImVec2(btnW, 0))) { tmp = std::max(0, tmp-1); changed = true; } Tip("Image: 0 | Vidéo: image suivante");
            ImGui::SameLine(0.0f, pad);
            if (ImGui::Button("+", ImVec2(btnW, 0))) { tmp = std::min(std::max(tot,0), tmp+1); changed = true; } Tip("Image: 0 | Vidéo: image précédente");
        } else {
            if (ImGui::Button("+", ImVec2(btnW, 0))) { tmp = std::min(std::max(tot,0), tmp+1); changed = true; } Tip("Image: 0 | Vidéo: image suivante");
            ImGui::SameLine(0.0f, pad);
            if (ImGui::Button("-", ImVec2(btnW, 0))) { tmp = std::max(0, tmp-1); changed = true; } Tip("Image: 0 | Vidéo: image précédente");
        }
        ImGui::PopStyleVar();
    };

    auto drawInput = [&](){
        ImGui::SetNextItemWidth(inputW);
        if (ImGui::InputInt("##in", &tmp, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)
            || ImGui::IsItemDeactivatedAfterEdit())
        {
            tmp = std::clamp(tmp, 0, std::max(tot,0));
            changed = true;
        }
        Tip("Aller directement à l'index (Enter pour valider)");
    };

    ImVec2 sliderMin, sliderMax;

    if (!alignRight) {
        drawButtonsPlusMinus(true);
        ImGui::SameLine(0.0f, pad);
        drawInput();
        ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(sliderW);
        bool slid = ImGui::SliderInt("##s", &tmp, 0, std::max(tot,0), " ", ImGuiSliderFlags_AlwaysClamp);
        changed = changed || slid;
        Tip("Scrub timeline");
        sliderMin = ImGui::GetItemRectMin(); sliderMax = ImGui::GetItemRectMax();
    } else {
        ImGui::SetNextItemWidth(sliderW);
        bool slid = ImGui::SliderInt("##s", &tmp, 0, std::max(tot,0), " ", ImGuiSliderFlags_AlwaysClamp);
        changed = changed || slid;
        Tip("Scrub timeline");
        sliderMin = ImGui::GetItemRectMin(); sliderMax = ImGui::GetItemRectMax();
        ImGui::SameLine(0.0f, gap);
        drawInput();
        ImGui::SameLine(0.0f, pad);
        drawButtonsPlusMinus(false);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    char label[64]; snprintf(label,64,"%d / %d", tmp, std::max(tot,0));
    ImVec2 ts = ImGui::CalcTextSize(label);
    float cx = (sliderMin.x + sliderMax.x - ts.x) * 0.5f;
    float cy = (sliderMin.y + sliderMax.y - ts.y) * 0.5f;
    dl->AddText(ImVec2(cx,cy), IM_COL32(220,220,235,255), label);

    if (changed){
        idx = tmp;
        if (playing) {
            next_frame_time = std::chrono::steady_clock::now();
            if(!isImage1) cap1.set(cv::CAP_PROP_POS_FRAMES, idx);
            if(!isImage2) cap2.set(cv::CAP_PROP_POS_FRAMES, linked ? idx : frame2_idx);
        }
    }
    ImGui::PopID();
    return changed;
}

static bool CirclePlayButton(const char* id, float r){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c = ImVec2(p.x + r, p.y + r);
    ImGui::InvisibleButton("##btn", ImVec2(2*r, 2*r));
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemClicked();

    ImU32 base = playing ? IM_COL32(220,  60, 60,255)  : IM_COL32( 60, 180, 85,255);
    ImU32 hov  = playing ? IM_COL32(240,  80, 80,255)  : IM_COL32( 85, 205,110,255);
    dl->AddCircleFilled(c, r, hovered?hov:base);

    if (playing) {
        float w = r*0.45f;
        dl->AddRectFilled(ImVec2(c.x-w*1.4f, c.y-w), ImVec2(c.x-w*0.6f, c.y+w), IM_COL32_WHITE, 2);
        dl->AddRectFilled(ImVec2(c.x+w*0.6f, c.y-w), ImVec2(c.x+w*1.4f, c.y+w), IM_COL32_WHITE, 2);
    } else {
        ImVec2 a(c.x - r*0.35f, c.y - r*0.5f);
        ImVec2 b(c.x - r*0.35f, c.y + r*0.5f);
        ImVec2 d(c.x + r*0.55f, c.y);
        dl->AddTriangleFilled(a,b,d, IM_COL32_WHITE);
    }
    Tip("Lecture / Pause (Espace)");
    ImGui::PopID();
    return pressed;
}

static float ActionsRowWidth(){
    auto& st = ImGui::GetStyle();
    float sp = st.ItemSpacing.x;
    float h  = ImGui::GetFrameHeight();
    auto checkW = [&](const char* txt){ return h + sp + ImGui::CalcTextSize(txt).x; };
    float w = 0;
    w += checkW("Sub") + sp;
    w += checkW("Overlay") + sp;
    w += checkW("Blink") + sp;
    w += checkW("Click") + sp;
    w += 32.0f + sp;                 
    w += ImGui::CalcTextSize("Speed").x + 110.0f + sp; 
    w += ImGui::CalcTextSize("Reset frame").x + st.FramePadding.x*2 + sp;
    w += ImGui::CalcTextSize("Reset zoom").x  + st.FramePadding.x*2 + sp;
    w += ImGui::CalcTextSize("Link Zoom: OFF").x + st.FramePadding.x*2 + sp; 
    w += ImGui::CalcTextSize("Métriques: ON").x + st.FramePadding.x*2;
    return w;
}

static void drawVideo(int w,int h){
    if(!tex1||!tex2) return;
    ImDrawList*dl=ImGui::GetWindowDrawList();
    ImVec2 pos=ImGui::GetCursorScreenPos(),avail=ImGui::GetContentRegionAvail();
    float sc=std::min(avail.x/w,avail.y/h),ww=w*sc,hh=h*sc;
    ImVec2 p1{pos.x+(avail.x-ww)*.5f,pos.y+(avail.y-hh)*.5f}, p2{p1.x+ww,p1.y+hh};
    ImGuiIO&io=ImGui::GetIO(); bool hover=ImGui::IsMouseHoveringRect(p1,p2);
    static bool drag=false;

    auto zoomPan=[&](float&z,ImVec2&c,bool left){
        bool side = (io.MousePos.x < p1.x + ww*wipe_pos);
        if(hover && io.MouseWheel!=0 && ((left&&side)||(!left&&!side))){
            float nz=std::clamp(z*(1.f+io.MouseWheel*.1f),1.f,8.f);
            ImVec2 mu{(io.MousePos.x-p1.x)/ww,(io.MousePos.y-p1.y)/hh};
            c=mu+(c-mu)*(z/nz); z=nz;
            if(zoom_linked) { if(left) { zoom_r=z; center_r=c; } else { zoom_l=z; center_l=c; } }
        }
        if(io.MouseDown[ImGuiMouseButton_Middle]){
            ImVec2 d{io.MouseDelta.x/ww/z,io.MouseDelta.y/hh/z};
            if(left){ c.x=std::clamp(c.x-d.x,0.f,1.f); c.y=std::clamp(c.y-d.y,0.f,1.f); }
            else    { center_r.x=std::clamp(center_r.x-d.x,0.f,1.f); center_r.y=std::clamp(center_r.y-d.y,0.f,1.f); }
            if(zoom_linked) { if(left) center_r=c; else center_l=center_r; }
        }
    };
    
    zoomPan(zoom_l,center_l,true); 
    zoomPan(zoom_r,center_r,false);

    // NEW: ROI drawing mode — click-drag to define the ROI rectangle
    static bool roi_dragging = false;
    static ImVec2 roi_drag_start;
    if(roi_editing && hover) {
        if(io.MouseDown[ImGuiMouseButton_Left]) {
            ImVec2 norm{std::clamp((io.MousePos.x - p1.x)/ww, 0.f, 1.f),
                        std::clamp((io.MousePos.y - p1.y)/hh, 0.f, 1.f)};
            if(!roi_dragging) { roi_drag_start = norm; roi_dragging = true; }
            roi_min = ImVec2(std::min(roi_drag_start.x, norm.x), std::min(roi_drag_start.y, norm.y));
            roi_max = ImVec2(std::max(roi_drag_start.x, norm.x), std::max(roi_drag_start.y, norm.y));
        } else if(roi_dragging) {
            // Finished drawing
            roi_dragging = false;
            roi_editing = false;
            // Enable ROI if user drew a non-trivial area
            if((roi_max.x - roi_min.x) > 0.02f && (roi_max.y - roi_min.y) > 0.02f) {
                roi_enabled = true;
            }
        }
    }

    bool onH=std::fabs(io.MousePos.x-(p1.x+ww*wipe_pos))<12 && io.MousePos.y>p1.y && io.MousePos.y<p2.y;
    // Disable wipe drag while drawing ROI
    if(onH && hover && !roi_editing){ ImGui::SetTooltip("Glissez pour déplacer le séparateur"); } 

    if(io.MouseDown[ImGuiMouseButton_Left]&&onH && !roi_editing) drag=true;
    if(!io.MouseDown[ImGuiMouseButton_Left]) drag=false;
    if(drag) wipe_pos=std::clamp((io.MousePos.x-p1.x)/ww,0.f,1.f);
    if(!ImGui::IsAnyMouseDown()&&std::fabs(wipe_pos-.5f)<.03f) wipe_pos=.5f;

    auto uvCalc=[&](ImVec2 c,float z){ float hw=.5f/z,hhf=.5f/z;
        ImVec2 a{c.x-hw,c.y-hhf},b{c.x+hw,c.y+hhf};
        a.x=std::clamp(a.x,0.f,1.f); a.y=std::clamp(a.y,0.f,1.f);
        b.x=std::clamp(b.x,0.f,1.f); b.y=std::clamp(b.y,0.f,1.f);
        return std::pair(a,b);
    };
    auto [uL0,uL1]=uvCalc(center_l,zoom_l);
    auto [uR0,uR1]=uvCalc(center_r,zoom_r);
    float split=p1.x+ww*wipe_pos;

    auto drawDominantLabel=[&](const char* name){
        ImVec2 tsize = ImGui::CalcTextSize(name);
        ImVec2 posC  = ImVec2((p1.x+p2.x - tsize.x)*0.5f, p1.y + 6.0f);
        dl->AddRectFilled(ImVec2(posC.x-6,posC.y-3), ImVec2(posC.x+tsize.x+6,posC.y+tsize.y+3),
                          IM_COL32(0,0,0,160), 5.0f);
        dl->AddText(ImVec2(posC.x,posC.y), IM_COL32(255,255,255,255), name);
    };

    if(view_mode==VM_SUB && tex_sub)
        dl->AddImage((ImTextureID)(intptr_t)tex_sub,p1,p2,uL0,uL1);
    else if(view_mode==VM_OVERLAY){
        dl->AddImage((ImTextureID)(intptr_t)tex1,p1,p2,uL0,uL1);
        dl->AddImage((ImTextureID)(intptr_t)tex2,p1,p2,uR0,uR1, IM_COL32(255,255,255,int(effect_intensity*255)));
        drawDominantLabel(effect_intensity>=0.5f ? filenameOnly(path2).c_str()
                                                 : filenameOnly(path1).c_str());
    }else if(view_mode==VM_BLINK){
        static float t=0; t+=ImGui::GetIO().DeltaTime;
        float per=.25f+(1.f-effect_intensity)*2.0f;
        bool show1=fmod(t,per)<per*.5f;
        GLuint tex=show1?tex1:tex2; auto u0=show1?uL0:uR0, u1=show1?uL1:uR1;
        dl->AddImage((ImTextureID)(intptr_t)tex,p1,p2,u0,u1);
        drawDominantLabel(show1?filenameOnly(path1).c_str():filenameOnly(path2).c_str());
    }else if(view_mode==VM_CLICK){
        bool show2=ImGui::GetIO().MouseDown[ImGuiMouseButton_Left];
        GLuint tex=show2?tex2:tex1; auto u0=show2?uR0:uL0, u1=show2?uR1:uL1;
        dl->AddImage((ImTextureID)(intptr_t)tex,p1,p2,u0,u1);
        drawDominantLabel(show2?filenameOnly(path2).c_str():filenameOnly(path1).c_str());
    }else{
        dl->PushClipRect(p1,{split,p2.y},true);
        dl->AddImage((ImTextureID)(intptr_t)tex1,p1,p2,uL0,uL1); dl->PopClipRect();
        dl->PushClipRect({split,p1.y},p2,true);
        dl->AddImage((ImTextureID)(intptr_t)tex2,p1,p2,uR0,uR1); dl->PopClipRect();

        ImU32 lineCol = IM_COL32(240,240,240,255);
        ImU32 ringCol = IM_COL32( 30, 30, 30,255);
        ImU32 dotCol  = IM_COL32(255,255,255,255);
        dl->AddLine({split,p1.y},{split,p2.y}, lineCol, 3.0f);
        ImVec2 cc{split, p1.y + hh*0.5f};
        dl->AddCircleFilled(cc, 15.0f, lineCol);
        dl->AddCircleFilled(cc, 12.0f, ringCol);
        dl->AddCircleFilled(cc,  4.0f, dotCol);
    }

    auto hud=[&](float z,float x,float y){
        char buf[8]; snprintf(buf,8,"%.1fx",z);
        ImVec2 ts=ImGui::CalcTextSize(buf);
        dl->AddRectFilled({x-3,y-3},{x+ts.x+7,y+ts.y+5},IM_COL32(0,0,0,190),4);
        dl->AddText({x+2,y},IM_COL32(255,255,255,255),buf);
    };
    hud(zoom_l,p1.x+8,p1.y+8);
    hud(zoom_r,p2.x-ImGui::CalcTextSize("8.8x").x-12,p1.y+8);

    // NEW: Draw ROI rectangle on top of everything
    if(roi_enabled || roi_editing) {
        ImVec2 r1{p1.x + roi_min.x * ww, p1.y + roi_min.y * hh};
        ImVec2 r2{p1.x + roi_max.x * ww, p1.y + roi_max.y * hh};
        ImU32 roiCol = roi_editing ? IM_COL32(255, 200, 60, 220) : IM_COL32(60, 200, 255, 200);
        // Semi-transparent fill
        dl->AddRectFilled(r1, r2, IM_COL32(roi_editing?255:60, roi_editing?200:200, roi_editing?60:255, 30));
        // Border
        dl->AddRect(r1, r2, roiCol, 0.0f, 0, 2.0f);
        // Label
        const char* roiLbl = roi_editing ? "ROI (dessin)" : "ROI";
        ImVec2 lblPos{r1.x + 4, r1.y + 4};
        ImVec2 lblSz = ImGui::CalcTextSize(roiLbl);
        dl->AddRectFilled({lblPos.x-2, lblPos.y-2}, {lblPos.x+lblSz.x+4, lblPos.y+lblSz.y+2}, IM_COL32(0,0,0,160), 3.0f);
        dl->AddText(lblPos, roiCol, roiLbl);
    }
}

#ifdef _WIN32
static void ApplyWindowIcon(SDL_Window* win) {
    HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                    MAKEINTRESOURCEW(1),
                                    IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON),
                                    GetSystemMetrics(SM_CYICON),
                                    0);
    if (!hIcon) {
        wchar_t modPath[MAX_PATH]{0};
        GetModuleFileNameW(nullptr, modPath, MAX_PATH);
        std::filesystem::path p = std::filesystem::path(modPath).parent_path() / L"assets" / L"icon.ico";
        if (std::filesystem::exists(p)) {
            hIcon = (HICON)LoadImageW(nullptr, p.c_str(), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON),
                                      LR_LOADFROMFILE);
        }
    }
    if (!hIcon) return;
    SDL_SysWMinfo wminfo; SDL_VERSION(&wminfo.version);
    if (SDL_GetWindowWMInfo(win, &wminfo)) {
        HWND hwnd = wminfo.info.win.window;
        SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
}
#endif

int main(){
    // FORCE HIDE CONSOLE (Si le CMake n'a pas fonctionné)
    FreeConsole();
    
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);

    loadIni();
    // Cache la console au démarrage si l'option est OFF
    ToggleConsole(show_console);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Init(SDL_INIT_VIDEO); TTF_Init();
    SDL_Window*win=SDL_CreateWindow("V.I.C - Video Image Comparator v2.1",saved_win_x,saved_win_y,saved_win_w,saved_win_h,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    if(!win){ SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError()); return 1; }
    if(saved_win_maximized) SDL_MaximizeWindow(win);
#ifdef _WIN32
    ApplyWindowIcon(win);
#endif
    SDL_GLContext gl=SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(vsync_enabled?1:0);
    glewInit();
    SetupGLPackingOnce();

    ImGui::CreateContext(); uiStyle();
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDL2_InitForOpenGL(win,gl);
    ImGui_ImplOpenGL3_Init("#version 130");
    SDL_EventState(SDL_DROPFILE,SDL_ENABLE);
#if SDL_VERSION_ATLEAST(2,0,5)
    SDL_EventState(SDL_DROPBEGIN,SDL_ENABLE);
    SDL_EventState(SDL_DROPCOMPLETE,SDL_ENABLE);
#endif

    const float ctrlH=245.f;
    const float speedVals[]={.25f,.5f,.75f,1.f,1.25f,1.5f,1.75f,2.f,3.f,4.f};
    const char* speedLbls[]={"0.25x","0.5x","0.75x","1x","1.25x","1.5x","1.75x","2x","3x","4x"};
    int spIdx = 3; for(int i=0;i<(int)IM_ARRAYSIZE(speedVals);++i) if(fabs(speedVals[i]-playback_speed)<1e-6) spIdx=i;

    bool done=false,popup=true;
    std::vector<std::string> dropFiles; bool collectingDrop=false; int dropMouseX=0;

    auto loadAboutTex=[&](){
        if(tex_about) return;
        const std::string path = findAboutImage();
        if(!path.empty()){
            cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
            if(!img.empty()) uploadImageTo(tex_about, about_w, about_h, img);
        }
    };

    while(!done){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            ImGui_ImplSDL2_ProcessEvent(&e);
            if(e.type==SDL_QUIT) done=true;

#if SDL_VERSION_ATLEAST(2,0,5)
            if(e.type==SDL_DROPBEGIN){ collectingDrop=true; dropFiles.clear(); SDL_GetMouseState(&dropMouseX,nullptr); }
            if(e.type==SDL_DROPCOMPLETE){
                if(!dropFiles.empty()){
                    int ww; SDL_GetWindowSize(win,&ww,nullptr); bool left = dropMouseX<ww/2;
                    if(!loaded){
                        if(dropFiles.size()>=1) { if(left) path1=dropFiles[0]; else path2=dropFiles[0]; }
                        if(dropFiles.size()>=2) { if(left) path2=dropFiles[1]; else path1=dropFiles[1]; }
                        if(!path1.empty() && !path2.empty()){ 
                            popup=false; need_close_load_popup=true; 
                            path1_safe=path1; path2_safe=path2;
                            addRecent(path1, path2);
                            StartLoading(path1, path2); 
                        }
                    }else{
                        if(dropFiles.size()>=1){
                            if(left) { path1=dropFiles[0]; StartLoading(path1, ""); } 
                            else { path2=dropFiles[0]; StartLoading("", path2); }
                        }
                    }
                }
                collectingDrop=false;
            }
#endif
            if(e.type==SDL_DROPFILE){
                char* f = e.drop.file;
                if(collectingDrop) dropFiles.emplace_back(f);
                else{
                    int mx; SDL_GetMouseState(&mx,nullptr); int ww; SDL_GetWindowSize(win,&ww,nullptr);
                    bool left = mx<ww/2;
                    std::string fpath(f);
                    
                    if(!loaded){
                        if(left && path1.empty()) path1=fpath;
                        else if(!left && path2.empty()) path2=fpath;
                        else { if(left) path2=fpath; else path1=fpath; }
                        if(!path1.empty() && !path2.empty()){ 
                            popup=false; need_close_load_popup=true; 
                            path1_safe=path1; path2_safe=path2;
                            addRecent(path1, path2);
                            StartLoading(path1, path2);
                        }
                    }else{
                        if(left) { path1=fpath; StartLoading(fpath, path2); }
                        else { path2=fpath; StartLoading(path1, fpath); }
                    }
                }
                SDL_free(f);
            }
        } 

        if (is_loading_async) {
            if (loader_future.valid() && loader_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                loader_future.get(); 

                if(g_res.s1) {
                    cap1 = std::move(g_res.c1); cap1_seek = std::move(g_res.cs1);
                    info1 = g_res.i1; isImage1 = g_res.img1; img1_static = g_res.sImg1; frame1 = g_res.m1;
                    if(!g_res.p1_safe_out.empty()) path1_safe = g_res.p1_safe_out;
                    if(!frame1.empty()) { uploadMat(tex1, tex1_w, tex1_h, frame1); frame1_fresh = true; }
                }
                if(g_res.s2) {
                    cap2 = std::move(g_res.c2); cap2_seek = std::move(g_res.cs2);
                    info2 = g_res.i2; isImage2 = g_res.img2; img2_static = g_res.sImg2; frame2 = g_res.m2;
                    if(!g_res.p2_safe_out.empty()) path2_safe = g_res.p2_safe_out;
                    if(!frame2.empty()) { uploadMat(tex2, tex2_w, tex2_h, frame2); frame2_fresh = true; }
                }
                
                if (use_original_fps && info1.fps > 0) video_fps = info1.fps;

                if(g_res.s1 || g_res.s2) loaded=true;
                
                frame1_idx=frame2_idx=0; zoom_l=zoom_r=1.f; center_l=center_r={.5f,.5f};
                psnrHist.clear(); ssimHist.clear();
                next_frame_time=std::chrono::steady_clock::now();
                updateFrame();

                is_loading_async = false;
            }
        }

        if(loaded && !is_loading_async) updateFrame();

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoTitleBar;
        if(!cinema_mode) mainFlags |= ImGuiWindowFlags_MenuBar;
        ImGui::Begin("Main",nullptr,mainFlags);

        if(!cinema_mode && ImGui::BeginMenuBar()){
            if(ImGui::BeginMenu("Fichier")){
                if(ImGui::MenuItem("Média 1...")) fileDlg(path1); Tip("Choisir le média gauche (vidéo ou image)");
                if(ImGui::MenuItem("Média 2...")) fileDlg(path2); Tip("Choisir le média droit (vidéo ou image)");
                if(ImGui::MenuItem("Recharger")) { if(!path1.empty() && !path2.empty()) StartLoading(path1, path2); }
                ImGui::Separator();
                if(ImGui::BeginMenu("Fichiers récents")) {
                    if(recent_files.empty()) { ImGui::TextDisabled("(aucun)"); }
                    else {
                        int ri = 0;
                        for(const auto& [rp1, rp2] : recent_files) {
                            char label[512]; snprintf(label, sizeof(label), "%d. %s  |  %s", ri+1, filenameOnly(rp1).c_str(), filenameOnly(rp2).c_str());
                            if(ImGui::MenuItem(label)) { path1=rp1; path2=rp2; popup=false; need_close_load_popup=true; StartLoading(path1, path2); }
                            ++ri;
                        }
                        ImGui::Separator();
                        if(ImGui::MenuItem("Effacer l'historique")) { recent_files.clear(); saveIni(); }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if(ImGui::MenuItem("Quitter")) done=true; Tip("Fermer l'application");
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Options")){
                if(ImGui::MenuItem("Vitesse originale",nullptr,use_original_fps)){ use_original_fps=!use_original_fps; saveIni(); StartLoading(path1, path2); } Tip("Lire aux FPS d'origine (sinon 30 FPS)");
                if(ImGui::MenuItem("VSync", nullptr, vsync_enabled)){ vsync_enabled=!vsync_enabled; saveIni(); SDL_GL_SetSwapInterval(vsync_enabled?1:0); } Tip("Synchroniser le swap avec l'écran");
                if(ImGui::MenuItem("Afficher Console", nullptr, show_console)){ show_console=!show_console; ToggleConsole(show_console); saveIni(); } Tip("Afficher/Masquer la fenêtre de commande");
                if(ImGui::MenuItem("Métriques (PSNR/SSIM)",nullptr,metrics_on)){ metrics_on=!metrics_on; saveIni(); } Tip("Activer l'affichage PSNR/SSIM/MSE");
                if(ImGui::BeginMenu("HW selector")){
                    bool auto_hw = (hw_backend == HWBackend::AUTO);
                    bool dxva = (hw_backend == HWBackend::DSHOW_DXVA2);
                    bool cpu = (hw_backend == HWBackend::CPU_FFMPEG);
                    if(ImGui::MenuItem("AUTO (Prio FFMPEG -> Fallback DSHOW)", nullptr, auto_hw)){ hw_backend = HWBackend::AUTO; saveIni(); StartLoading(path1, path2); }
                    if(ImGui::MenuItem("Force DirectShow/DXVA2", nullptr, dxva)){ hw_backend = HWBackend::DSHOW_DXVA2; saveIni(); StartLoading(path1, path2); }
                    if(ImGui::MenuItem("Force CPU/FFMPEG", nullptr, cpu)){ hw_backend = HWBackend::CPU_FFMPEG; saveIni(); StartLoading(path1, path2); }
                    ImGui::EndMenu();
                }
                if(ImGui::MenuItem("Raccourcis...")) show_rebinding = true; Tip("Modifier les touches");
                ImGui::Separator();
                if(ImGui::BeginMenu("Mode Sync")) {
                    if(ImGui::MenuItem("Timestamp (recommandé)", nullptr, sync_mode==SyncMode::TIMESTAMP)) { sync_mode=SyncMode::TIMESTAMP; saveIni(); }
                    Tip("Synchronise par temps — idéal quand les FPS diffèrent");
                    if(ImGui::MenuItem("Frame Index", nullptr, sync_mode==SyncMode::FRAME_INDEX)) { sync_mode=SyncMode::FRAME_INDEX; saveIni(); }
                    Tip("Synchronise par numéro de frame");
                    ImGui::Separator();
                    ImGui::Text("Offset (frames) :");
                    ImGui::SetNextItemWidth(120);
                    if(ImGui::InputInt("##sync_offset", &sync_offset_frames, 1, 10)) { saveIni(); updateFrame(); }
                    Tip("Décalage appliqué à la vidéo 2 (positif = en avance)");
                    if(ImGui::Button("Reset offset")) { sync_offset_frames = 0; saveIni(); updateFrame(); }
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("ROI (zone métriques)")) {
                    if(ImGui::MenuItem("Activer ROI", nullptr, roi_enabled)) { roi_enabled = !roi_enabled; }
                    Tip("Limite PSNR/SSIM/heatmap à une zone");
                    if(ImGui::MenuItem("Dessiner ROI (clic-glisser)")) { roi_editing = true; }
                    Tip("Clique-glisse sur la vidéo pour définir la zone");
                    if(ImGui::MenuItem("Reset ROI (R)")) { resetRoi(); }
                    ImGui::EndMenu();
                }
                if(ImGui::MenuItem("Plein écran (F11)", nullptr, fullscreen)) { toggleFullscreen(win); }
                if(ImGui::MenuItem("Mode Cinéma (F10)", nullptr, cinema_mode)) { toggleCinema(); }
                Tip("Cache l'UI pour ne montrer que le lecteur");
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("À propos")){
                if(ImGui::MenuItem("À propos")){ about_open=true; }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (about_open) {
            ImGui::SetNextWindowSize(ImVec2(820, 600), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("A propos - V.I.C", &about_open, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar)) {
                if (!tex_about) { loadAboutTex(); }
                if (tex_about) {
                    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)tex_about, ImGui::GetWindowPos(), {ImGui::GetWindowPos().x+ImGui::GetWindowSize().x, ImGui::GetWindowPos().y+ImGui::GetWindowSize().y}, {0,0}, {1,1}, IM_COL32(255,255,255,153));
                }
                ImGui::Dummy(ImVec2(0, 24));
                ImVec2 cur = ImGui::GetCursorScreenPos();
                ImVec2 max = ImVec2(cur.x + ImGui::GetWindowSize().x - 60, cur.y + 400); 
                ImGui::GetWindowDrawList()->AddRectFilled(cur, max, IM_COL32(255,255,255,220), 10.0f);
                ImGui::Dummy(ImVec2(16, 12)); ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 550.0f); 
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,1));
                ImGui::SetWindowFontScale(1.2f);
                ImGui::Text("V.I.C - Video Image Comparator v2.1");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Dummy(ImVec2(0, 5));
                ImGui::TextWrapped("Outil de comparaison video et image professionnel pour analyser les differences de qualite entre deux sources (encodages, restaurations, filtres, etc.).");
                ImGui::TextWrapped("Fonctionnalites : Side-by-Side, Overlay, Blink, Click, Heatmap, PSNR/SSIM temps reel, ROI, Sync multi-fps, Cinema mode.");
                ImGui::Separator();
                ImGui::Text("Crédits :");
                ImGui::BulletText("Concept & Direction : Crysisjim");
                ImGui::BulletText("Grok 4.0 : 60%% (Base du code & Architecture)");
                ImGui::BulletText("ChatGPT 5 : 20%% (Amélioration & Optimisation)");
                ImGui::BulletText("Gemini 3 Pro : 20%% (Finalisation & Corrections)");
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::Text("Stack Technique :");
                ImGui::TextWrapped("C++20, OpenGL 3.3, ImGui, SDL2, OpenCV 4.x (FFMPEG/DSHOW)");
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::End();
            }
        }

        if(!loaded && popup) ImGui::OpenPopup("Load");
        ImGui::SetNextWindowSize({560,0},ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Always,{.5f,.5f});
        if(ImGui::BeginPopupModal("Load",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize)){
            if(need_close_load_popup){ ImGui::CloseCurrentPopup(); need_close_load_popup=false; }
            ImGui::TextWrapped("Sélectionnez deux médias (vidéo ou image), ou glissez-déposez.");
            
            // FIX: Safe buffer copy instead of const_cast UB
            char buf1[1024]; strncpy(buf1, path1.c_str(), sizeof(buf1)-1); buf1[sizeof(buf1)-1]=0;
            char buf2[1024]; strncpy(buf2, path2.c_str(), sizeof(buf2)-1); buf2[sizeof(buf2)-1]=0;
            ImGui::InputText("Média 1", buf1, sizeof(buf1), ImGuiInputTextFlags_ReadOnly); ImGui::SameLine(); if(ImGui::Button("...##1")) fileDlg(path1);
            ImGui::InputText("Média 2", buf2, sizeof(buf2), ImGuiInputTextFlags_ReadOnly); ImGui::SameLine(); if(ImGui::Button("...##2")) fileDlg(path2);
            
            if(ImGui::Button("CHARGER",ImVec2(-1,40))){ addRecent(path1, path2); StartLoading(path1, path2); popup=false; need_close_load_popup=true; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        if (is_loading_async) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
            ImGui::SetNextWindowSize({200, 100}, ImGuiCond_Always);
            ImGui::Begin("LoadingOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
            ImGui::SetCursorPos({(ImGui::GetWindowSize().x - ImGui::CalcTextSize("Chargement...").x) * 0.5f, 20});
            ImGui::Text("Chargement...");
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 center(ImGui::GetCursorScreenPos().x + (ImGui::GetWindowSize().x * 0.5f) - 20 + 20, ImGui::GetCursorScreenPos().y + 10 + 20);
            // FIX: Spinner now has animated opacity per dot
            float spinTime = (float)ImGui::GetTime() * 5.0f;
            for (int i = 0; i < 12; i++) {
                float angle = spinTime + i * 6.28f / 12;
                float alpha = fmodf((spinTime / 6.28f - (float)i / 12.0f + 1.0f), 1.0f);
                int a = (int)(alpha * 200.0f) + 55;
                dl->AddCircleFilled(ImVec2(center.x + cosf(angle) * 13.0f, center.y + sinf(angle) * 13.0f), 3.0f, IM_COL32(255, 255, 255, a));
            }
            ImGui::End();
        }

        if(loaded && !is_loading_async){
            ImVec2 avail=ImGui::GetContentRegionAvail();
            // Cinema mode: video takes full space, no controls visible
            float videoH = cinema_mode ? avail.y : std::max(avail.y-ctrlH,120.f);
            ImGui::BeginChild("vid",{avail.x,videoH},false,ImGuiWindowFlags_NoScrollbar);
            if(!cinema_mode) {
                ImGui::Columns(2,nullptr,false);
                {
                    std::string L = filenameOnly(path1);
                    float colW = ImGui::GetColumnWidth();
                    float tx = ImGui::GetCursorPosX() + (colW - ImGui::CalcTextSize(L.c_str()).x) * 0.5f;
                ImGui::SetCursorPosX(tx); ImGui::TextUnformatted(L.c_str());
                char buf[256]; snprintf(buf,256,"[%dx%d | %.2f FPS | %s (%s) | %.1f Mbps]",
                    info1.w,info1.h,info1.fps,
                    info1.fourcc.empty()?"????":info1.fourcc.c_str(),
                    info1.codecPretty.empty()?info1.codecTag.c_str():info1.codecPretty.c_str(),
                    info1.br);
                // FIX: Removed unused tx2 variable
                ImGui::SetCursorPosX((colW - ImGui::CalcTextSize(buf).x)*0.5f + ImGui::GetColumnOffset());
                ImGui::TextUnformatted(buf);
            }
            ImGui::NextColumn();
            {
                std::string R = filenameOnly(path2);
                float colW = ImGui::GetColumnWidth();
                ImGui::SetCursorPosX((colW - ImGui::CalcTextSize(R.c_str()).x)*0.5f + ImGui::GetColumnOffset());
                ImGui::TextUnformatted(R.c_str());
                char buf[256]; snprintf(buf,256,"[%dx%d | %.2f FPS | %s (%s) | %.1f Mbps]",
                    info2.w,info2.h,info2.fps,
                    info2.fourcc.empty()?"????":info2.fourcc.c_str(),
                    info2.codecPretty.empty()?info2.codecTag.c_str():info2.codecPretty.c_str(),
                    info2.br);
                ImGui::SetCursorPosX((colW - ImGui::CalcTextSize(buf).x)*0.5f + ImGui::GetColumnOffset());
                ImGui::TextUnformatted(buf);
            }
            ImGui::Columns(1);
            } // end if(!cinema_mode) — file info columns
            drawVideo(tex1_w?tex1_w:1920, tex1_h?tex1_h:1080);
            ImGui::EndChild();

            if(!cinema_mode) {
            // FIX: Use per-video frame counts instead of cap.get during rendering
            int tot1 = isImage1 ? 0 : std::max(0, info1.total_frames - 1);
            int tot2 = isImage2 ? 0 : std::max(0, info2.total_frames - 1);

            if (ImGui::BeginTable("frame_tbl", 3, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                ImGui::TableSetupColumn("C", ImGuiTableColumnFlags_WidthFixed,   0.0f);
                ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); if (FrameRowHalf("f1", frame1_idx, tot1, false, 24.0f)) { if (linked) frame2_idx = syncedFrame2FromFrame1(frame1_idx); updateFrame(); }
                ImGui::TableSetColumnIndex(1); 
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
                if (ImGui::Button(linked ? "LINKED" : "UNLINKED")) { linked = !linked; saveIni(); } Tip("Verrouille les deux timelines");
                ImGui::TableSetColumnIndex(2); if (FrameRowHalf("f2", frame2_idx, tot2, true, 24.0f)) { if (linked) frame1_idx = syncedFrame1FromFrame2(frame2_idx); updateFrame(); }
                ImGui::EndTable();
            }

            // Sync mode indicator when FPS differ OR offset is non-zero
            if(linked && (std::fabs(info1.fps - info2.fps) > 0.5f || sync_offset_frames != 0)) {
                char syncLabel[128];
                const char* modeName = (sync_mode == SyncMode::TIMESTAMP) ? "Timestamp" : "Frame Index";
                if(sync_offset_frames != 0)
                    snprintf(syncLabel, sizeof(syncLabel), "Sync: %s  |  Offset: %+d frames", modeName, sync_offset_frames);
                else
                    snprintf(syncLabel, sizeof(syncLabel), "Sync: %s", modeName);
                float tw = ImGui::CalcTextSize(syncLabel).x;
                ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
                ImGui::TextDisabled("%s", syncLabel);
                Tip("Options > Mode Sync pour changer / ajuster l'offset");
            }

            float totalW = ActionsRowWidth();
            float startX = std::max(0.0f, (ImGui::GetContentRegionAvail().x - totalW) * 0.5f);
            ImGui::Dummy(ImVec2(startX,0)); ImGui::SameLine();

            bool v;
            v=(view_mode==VM_SUB); if(ImGui::Checkbox("Sub",&v)){ view_mode=v?VM_SUB:VM_NONE; if(view_mode==VM_SUB) makeSubTex(frame1,frame2);} Tip("Heatmap (différences)"); ImGui::SameLine();
            v=(view_mode==VM_OVERLAY); if(ImGui::Checkbox("Overlay",&v)){ view_mode=v?VM_OVERLAY:VM_NONE; } Tip("Superposer R sur L"); ImGui::SameLine();
            v=(view_mode==VM_BLINK); if(ImGui::Checkbox("Blink",&v)){ view_mode=v?VM_BLINK:VM_NONE; } Tip("Clignoter L/R"); ImGui::SameLine();
            v=(view_mode==VM_CLICK); if(ImGui::Checkbox("Click",&v)){ view_mode=v?VM_CLICK:VM_NONE; } Tip("Basculer L/R au clic"); ImGui::SameLine();

            if (CirclePlayButton("play_btn", 16.0f)) { 
                playing=!playing; 
                next_frame_time=std::chrono::steady_clock::now(); 
                if(playing) {
                    if(!isImage1) { cap1.set(cv::CAP_PROP_POS_FRAMES, frame1_idx); cap1.read(frame1); frame1_fresh=true; }
                    if(!isImage2) { cap2.set(cv::CAP_PROP_POS_FRAMES, linked ? syncedFrame2FromFrame1(frame1_idx) : frame2_idx); cap2.read(frame2); frame2_fresh=true; }
                }
            }
            ImGui::SameLine();
            ImGui::Text("Speed"); ImGui::SameLine(); ImGui::SetNextItemWidth(110);
            if(ImGui::Combo("##spd",&spIdx,speedLbls,IM_ARRAYSIZE(speedLbls))){ playback_speed=speedVals[spIdx]; saveIni(); } Tip("Vitesse de lecture");
            ImGui::SameLine();
            if(ImGui::Button("Reset frame")){ frame1_idx=frame2_idx=0; updateFrame(); } Tip("Revenir au début"); ImGui::SameLine();
            if(ImGui::Button("Reset zoom")){ zoom_l=zoom_r=1.f; center_l=center_r={.5f,.5f}; } Tip("Réinitialiser zoom/pan"); ImGui::SameLine();
            if(ImGui::Button(zoom_linked ? "Link Zoom: ON" : "Link Zoom: OFF")) { zoom_linked = !zoom_linked; saveIni(); } Tip("Lier Zoom et Panoramique"); ImGui::SameLine();
            
            if(ImGui::Button(metrics_on?"Métriques: ON":"Métriques: OFF")){ metrics_on=!metrics_on; saveIni(); } Tip("Afficher PSNR/SSIM/MSE");

            if(view_mode==VM_OVERLAY || view_mode==VM_BLINK){
                ImGui::SameLine(); ImGui::Text("Intensity"); ImGui::SameLine(); ImGui::SetNextItemWidth(180);
                ImGui::SliderFloat("##Intensity",&effect_intensity,0.f,1.f); Tip("0 = L, 1 = R (Overlay) | Durée blink");
            }

            ImGui::Dummy(ImVec2(0,10));
            if (metrics_on){
                char hdr[96]; snprintf(hdr, sizeof(hdr), "PSNR %.2f   |   SSIM %.4f   |   MSE %.1f", psnr, ssim, mse);
                float xpadHdr = std::max(0.0f, (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(hdr).x) * 0.5f);
                ImGui::Dummy(ImVec2(xpadHdr, 0)); ImGui::SameLine();
                ImGui::TextUnformatted(hdr); 
                
                // TOOLTIPS METRIQUES SEPARES
                if(ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("- PSNR (Peak Signal-to-Noise Ratio) : Mesure la fidélité (>40dB = Excellent).");
                    ImGui::Text("- SSIM (Structural Similarity) : Mesure la similarité visuelle (1.0 = Identique).");
                    ImGui::Text("- MSE (Mean Squared Error) : Moyenne des erreurs au carré (Plus c'est bas, mieux c'est).");
                    ImGui::EndTooltip();
                }

                float w = 1120.0f, h = 42.0f, xpad = std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f);
                ImGui::Dummy(ImVec2(xpad, 0)); ImGui::SameLine(); if(!psnrHist.empty()) ImGui::PlotLines("PSNR",psnrHist.data(),(int)psnrHist.size(),0,nullptr,0,60,ImVec2(w,h)); Tip("Graphique PSNR");
                ImGui::Dummy(ImVec2(xpad, 0)); ImGui::SameLine(); if(!ssimHist.empty()) ImGui::PlotLines("SSIM",ssimHist.data(),(int)ssimHist.size(),0,nullptr,0,1,ImVec2(w,h)); Tip("Graphique SSIM");
            }

            // INFO FOOTER
            {
                const char* hwName = "";
                if(hw_backend==HWBackend::DSHOW_DXVA2) hwName="DirectShow";
                else if(hw_backend==HWBackend::CPU_FFMPEG) hwName="FFMPEG";
                else hwName="AUTO";

                float fps = 0.0f;
                if(playing) fps = (float)(video_fps * playback_speed);
                else fps = 0.0f;

                const char* syncName = (sync_mode == SyncMode::TIMESTAMP) ? "TS" : "FI";
                char buf[200];
                if(roi_enabled)
                    snprintf(buf, 200, "Speed: %.2fx  |  HW: %s  |  FPS: %.2f  |  Sync: %s  |  ROI: ON", playback_speed, hwName, fps, syncName);
                else
                    snprintf(buf, 200, "Speed: %.2fx  |  HW: %s  |  FPS: %.2f  |  Sync: %s", playback_speed, hwName, fps, syncName);

                float lineH = ImGui::GetTextLineHeightWithSpacing();
                float winRight = ImGui::GetWindowContentRegionMax().x;
                float x = winRight - ImGui::CalcTextSize(buf).x - 10.0f;
                float y = ImGui::GetWindowContentRegionMax().y - lineH + 5.0f;
                ImGui::SetCursorPos(ImVec2(x, y));
                ImGui::TextDisabled(buf);
            }
            } // end if(!cinema_mode) — all controls/metrics/footer

            auto doStep=[&](int d){ 
                int t1=isImage1?0:std::max(0, info1.total_frames-1); 
                frame1_idx=std::clamp(frame1_idx+d,0,t1); 
                if(linked) frame2_idx = syncedFrame2FromFrame1(frame1_idx);
                updateFrame(); 
            };
            for(const auto& s : g_keys){
                if(matchShortcut(s, io)){
                    if(strcmp(s.name,"Play/Pause")==0) {
                        playing=!playing;
                        next_frame_time=std::chrono::steady_clock::now();
                        if(playing) {
                            if(!isImage1) cap1.set(cv::CAP_PROP_POS_FRAMES, frame1_idx);
                            if(!isImage2) cap2.set(cv::CAP_PROP_POS_FRAMES, linked ? syncedFrame2FromFrame1(frame1_idx) : frame2_idx);
                        }
                    }
                    else if(strcmp(s.name,"Frame +1")==0) doStep(1);
                    else if(strcmp(s.name,"Frame -1")==0) doStep(-1);
                    else if(strcmp(s.name,"Seek +10")==0) doStep(10);
                    else if(strcmp(s.name,"Seek -10")==0) doStep(-10);
                    else if(strcmp(s.name,"Home (0)")==0) { frame1_idx=0; frame2_idx=linked?syncedFrame2FromFrame1(0):frame2_idx; updateFrame(); }
                    else if(strcmp(s.name,"End (last)")==0) { frame1_idx=isImage1?0:std::max(0,info1.total_frames-1); if(linked) frame2_idx=syncedFrame2FromFrame1(frame1_idx); updateFrame(); }
                    else if(strcmp(s.name,"+ Speed")==0) { spIdx=std::min(spIdx+1,9); playback_speed=speedVals[spIdx]; saveIni(); }
                    else if(strcmp(s.name,"- Speed")==0) { spIdx=std::max(spIdx-1,0); playback_speed=speedVals[spIdx]; saveIni(); }
                    else if(strcmp(s.name,"Mode: Sub")==0)     { view_mode=(view_mode==VM_SUB)?VM_NONE:VM_SUB; if(view_mode==VM_SUB) makeSubTex(frame1,frame2); }
                    else if(strcmp(s.name,"Mode: Overlay")==0) { view_mode=(view_mode==VM_OVERLAY)?VM_NONE:VM_OVERLAY; }
                    else if(strcmp(s.name,"Mode: Blink")==0)   { view_mode=(view_mode==VM_BLINK)?VM_NONE:VM_BLINK; }
                    else if(strcmp(s.name,"Mode: Click")==0)   { view_mode=(view_mode==VM_CLICK)?VM_NONE:VM_CLICK; }
                    else if(strcmp(s.name,"Toggle Metrics")==0) { metrics_on=!metrics_on; saveIni(); }
                    else if(strcmp(s.name,"Toggle Link")==0)    { linked=!linked; saveIni(); }
                    else if(strcmp(s.name,"Toggle Zoom Link")==0) zoom_linked=!zoom_linked;
                    else if(strcmp(s.name,"Fullscreen")==0) toggleFullscreen(win);
                    else if(strcmp(s.name,"Cinema Mode")==0) toggleCinema();
                    else if(strcmp(s.name,"Reset ROI")==0) resetRoi();
                }
            }
        }

        if (show_rebinding) {
            ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Raccourcis clavier", &show_rebinding, ImGuiWindowFlags_AlwaysAutoResize)) {
                static int editIdx = -1;
                if (ImGui::BeginTable("tbl_keys", 2, ImGuiTableFlags_SizingStretchProp)) {
                    for (int i = 0; i < (int)g_keys.size(); ++i) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (ImGui::Selectable(g_keys[i].name, editIdx == i, ImGuiSelectableFlags_SpanAllColumns)) editIdx = i;
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", ImGui::GetKeyName(g_keys[i].key));
                    }
                    ImGui::EndTable();
                }
                if (ImGui::Button("Fermer")) show_rebinding = false;
                if (editIdx >= 0) {
                    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                        if (ImGui::IsKeyPressed((ImGuiKey)k)) { g_keys[editIdx].key=(ImGuiKey)k; editIdx=-1; break; }
                    }
                }
            }
            ImGui::End();
        }

        ImGui::End();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    // Nettoyage final des fichiers temp
    cleanupAliases();

    // NEW: Capture window geometry before saving (only if not fullscreen/maximized)
    Uint32 winFlags = SDL_GetWindowFlags(win);
    saved_win_maximized = (winFlags & SDL_WINDOW_MAXIMIZED) != 0;
    if(!fullscreen && !saved_win_maximized) {
        SDL_GetWindowPosition(win, &saved_win_x, &saved_win_y);
        SDL_GetWindowSize(win, &saved_win_w, &saved_win_h);
    }

    saveIni();
    // FIX: Delete ALL textures including tex_sub and tex_about
    if(tex1) glDeleteTextures(1,&tex1);
    if(tex2) glDeleteTextures(1,&tex2);
    if(tex_sub) glDeleteTextures(1,&tex_sub);
    if(tex_about) glDeleteTextures(1,&tex_about);
    
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(win);
    TTF_Quit(); SDL_Quit();
    return 0;
}
