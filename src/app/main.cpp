// Melovio Studio entry point: SDL2 + OpenGL3 + Dear ImGui (docking).
#include "app.hpp"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_internal.h"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace melovio;
using namespace melovio::ui;

namespace {
    struct Args {
        std::string open;
        int frames = -1;                    // > 0: render this many frames, then exit (for automated screenshots / smoke tests)
        std::string screenshot;             // BMP written on the last frame
        int width = 1500, height = 900;
        bool show_workshop = false, show_log = false, select_all = false, show_voicebanks_only = false;
        int select_note = -1;
        std::string lane;                   // parameter-lane id to show (dev aid, e.g. __velocity)
        std::string focus;                  // window name to focus before the screenshot
        std::string midi;                   // open the MIDI import dialog for this file at start-up (testing aid)
    std::string script;                 // UI automation script (see docs/DEVELOPMENT.md): click/drag/key/wait/shot
        bool help = false;
    };
    Args parse_args(int argc, char** argv) {
        Args a;
        for (int i = 1; i < argc; ++i) {
            std::string s = argv[i];
            auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
            if (s == "--help" || s == "-h") a.help = true;
            else if (s == "--open") a.open = next();
            else if (s == "--frames") a.frames = std::atoi(next().c_str());
            else if (s == "--screenshot") a.screenshot = next();
            else if (s == "--size") { std::sscanf(next().c_str(), "%dx%d", &a.width, &a.height); }
            else if (s == "--show-workshop") a.show_workshop = true;
            else if (s == "--show-log") a.show_log = true;
            else if (s == "--select-all") a.select_all = true;
            else if (s == "--select-note") a.select_note = std::atoi(next().c_str());
            else if (s == "--focus") a.focus = next();
            else if (s == "--lane") a.lane = next();
            else if (s == "--script") a.script = next();
            else if (s == "--import-midi") a.midi = next();
            else if (s[0] != '-') a.open = s;
        }
        return a;
    }

    void save_screenshot(SDL_Window* w, const std::string& path) {
        int ww = 0, hh = 0; SDL_GL_GetDrawableSize(w, &ww, &hh);
        using ReadPixelsFn = void (*)(int, int, int, int, unsigned, unsigned, void*);
        auto rp = reinterpret_cast<ReadPixelsFn>(SDL_GL_GetProcAddress("glReadPixels"));
        if (!rp || ww <= 0 || hh <= 0) { std::fprintf(stderr, "screenshot: glReadPixels unavailable\n"); return; }
        std::vector<unsigned char> px(static_cast<size_t>(ww) * static_cast<size_t>(hh) * 4);
        rp(0, 0, ww, hh, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
        std::vector<unsigned char> flipped(px.size());
        for (int y = 0; y < hh; ++y) std::memcpy(&flipped[static_cast<size_t>(y) * static_cast<size_t>(ww) * 4], &px[static_cast<size_t>(hh - 1 - y) * static_cast<size_t>(ww) * 4], static_cast<size_t>(ww) * 4);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), ww, hh, 32, ww * 4, SDL_PIXELFORMAT_RGBA32);
        if (s) { SDL_SaveBMP(s, path.c_str()); SDL_FreeSurface(s); }
    }

    struct Step { enum K { Move, Down, Up, Key, Wait, Shot, Text } k; int a, b, c; std::string str;
        Step(K k_, int a_ = 0, int b_ = 0, int c_ = 0, std::string s_ = {}) : k(k_), a(a_), b(b_), c(c_), str(std::move(s_)) {} };
    std::vector<Step> load_script(const std::string& path) {
        std::vector<Step> v; std::ifstream f(path); std::string line;
        auto keycode = [](const std::string& n) -> int { if (n == "space") return SDLK_SPACE; if (n == "delete") return SDLK_DELETE; if (n == "up") return SDLK_UP; if (n == "down") return SDLK_DOWN; if (n == "left") return SDLK_LEFT; if (n == "right") return SDLK_RIGHT; if (n == "enter") return SDLK_RETURN; return n.size() == 1 ? n[0] : 0; };
        while (std::getline(f, line)) {
            std::istringstream is(line); std::string cmd; is >> cmd; if (cmd.empty() || cmd[0] == '#') continue;
            if (cmd == "click" || cmd == "dblclick" || cmd == "rclick") { int x, y; is >> x >> y; const int btn = cmd == "rclick" ? 3 : 1;
                for (int n = 0; n < (cmd == "dblclick" ? 2 : 1); ++n) {
                    v.push_back({ Step::Move, x, y }); v.push_back({ Step::Down, x, y, btn }); v.push_back({ Step::Up, x, y, btn });
                } }
            else if (cmd == "drag") { int x1, y1, x2, y2; is >> x1 >> y1 >> x2 >> y2; v.push_back({ Step::Move, x1, y1 }); v.push_back({ Step::Down, x1, y1, 1 });
                for (int i = 1; i <= 6; ++i) { v.push_back({ Step::Move, x1 + (x2 - x1) * i / 6, y1 + (y2 - y1) * i / 6 }); }
                v.push_back({ Step::Up, x2, y2, 1 }); }
            else if (cmd == "move") { int x, y; is >> x >> y; v.push_back({ Step::Move, x, y }); }
            else if (cmd == "key") { std::string k; is >> k; int mod = 0; for (;;) { auto p = k.find('+'); if (p == std::string::npos) break; std::string m = k.substr(0, p); k = k.substr(p + 1); if (m == "ctrl") mod |= KMOD_CTRL; if (m == "shift") mod |= KMOD_SHIFT; }
                v.push_back({ Step::Key, keycode(k), mod, 1 }); v.push_back({ Step::Key, keycode(k), mod, 0 }); }
            else if (cmd == "text") { std::string t; std::getline(is, t); if (!t.empty() && t[0] == ' ') t.erase(0, 1); v.push_back(Step(Step::Text, 0, 0, 0, t)); }
            else if (cmd == "wait") { int n = 1; is >> n; v.push_back({ Step::Wait, n }); }
            else if (cmd == "shot") { std::string path_; is >> path_; v.push_back(Step(Step::Shot, 0, 0, 0, path_)); }
        }
        return v;
    }

    void build_default_layout(ImGuiID dock, const ImVec2& size) {
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, size);
        ImGuiID center = dock, top = 0, left = 0, right = 0, bottom = 0;
        top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.075f, nullptr, &center);
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Transport", top);
        ImGui::DockBuilderDockWindow("Tracks", left);
        ImGui::DockBuilderDockWindow("Piano Roll", center);
        ImGui::DockBuilderDockWindow("Parameter Lanes", bottom); ImGui::DockBuilderDockWindow("Voicebanks", bottom); ImGui::DockBuilderDockWindow("Log & Diagnostics", bottom);
        ImGui::DockBuilderDockWindow("Phonemes & Lyrics", right); ImGui::DockBuilderDockWindow("Note Properties", right);
        ImGui::DockBuilderFinish(dock);
    }
}

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);
    if (args.help) {
        std::printf("Melovio Studio %s\nusage: melovio-studio [project.mvp] [--open FILE] [--size WxH]\n"
                    "  test/automation: --lane ID  --frames N  --screenshot out.bmp  --show-workshop  --show-log  --select-all  --select-note N  --focus \"Window Name\"\n", MELOVIO_VERSION);
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_Window* window = SDL_CreateWindow("Melovio Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, args.width, args.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { std::fprintf(stderr, "cannot create window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) { std::fprintf(stderr, "cannot create an OpenGL context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(args.frames > 0 ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    if (!args.script.empty()) io.MouseDoubleClickTime = 5.0f;     // scripted clicks run at software-render frame rates; keep double-clicks recognisable
    std::error_code ec; fs::create_directories(default_config_dir(), ec);
    static std::string ini = path_to_utf8(default_config_dir() / "melovio_layout.ini");
    io.IniFilename = args.frames > 0 ? nullptr : ini.c_str();       // automated runs neither read nor write the user's layout

    App app;
    {
        // Honour the display's pixel density for the text size.
        int dw = 0, dh = 0, ww = 0, wh = 0; SDL_GL_GetDrawableSize(window, &dw, &dh); SDL_GetWindowSize(window, &ww, &wh);
        const float dpi_scale = ww > 0 ? static_cast<float>(dw) / static_cast<float>(ww) : 1.f;
        app.jp_font_loaded = load_japanese_font(16.f * dpi_scale);
        io.FontGlobalScale = 1.f / dpi_scale;
    }
    apply_theme(1.0f);
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    // SDL may hand back an OpenGL ES context (e.g. Mesa over EGL, Wayland); pick the matching GLSL dialect.
    using GetStringFn = const unsigned char* (*)(unsigned);
    auto gl_get_string = reinterpret_cast<GetStringFn>(SDL_GL_GetProcAddress("glGetString"));
    const char* gl_ver = gl_get_string ? reinterpret_cast<const char*>(gl_get_string(0x1F02 /*GL_VERSION*/)) : nullptr;
    const bool is_es = gl_ver && std::strstr(gl_ver, "OpenGL ES");
    ImGui_ImplOpenGL3_Init(is_es ? "#version 300 es" : "#version 130");

    fs::path exe = fs::path(argv[0]).has_parent_path() ? fs::absolute(fs::path(argv[0])).parent_path() : fs::current_path();
    if (char* base = SDL_GetBasePath()) { exe = fs::path(base); SDL_free(base); }
    app.init(exe, args.open);
    app.show_workshop = args.show_workshop; app.show_log = args.show_log;
    if (!args.midi.empty()) load_midi_for_import(app, path_from_utf8(args.midi));
    if (args.select_all) app.select_all();
    if (!args.lane.empty()) app.lane_param = args.lane;
    if (args.select_note >= 0) if (Track* t = app.cur_track(); t && static_cast<size_t>(args.select_note) < t->notes.size()) app.select_only(t->notes[static_cast<size_t>(args.select_note)].id);

    std::vector<Step> script; size_t script_pos = 0; int script_wait = 0;
    if (!args.script.empty()) script = load_script(args.script);
    int frame = 0; std::string last_title;
    bool running = true;
    while (running && !app.quit_requested) {
        if (script_pos < script.size() && frame > 8) {
            if (script_wait > 0) --script_wait;
            else {
                const Step& st = script[script_pos++];
                SDL_Event ev; SDL_zero(ev);
                switch (st.k) {
                case Step::Move: SDL_WarpMouseInWindow(window, st.a, st.b); ev.type = SDL_MOUSEMOTION; ev.motion.windowID = SDL_GetWindowID(window); ev.motion.x = st.a; ev.motion.y = st.b; ev.motion.xrel = 1; ev.motion.yrel = 1; SDL_PushEvent(&ev); break;
                case Step::Down: case Step::Up: ev.type = st.k == Step::Down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; ev.button.windowID = SDL_GetWindowID(window); ev.button.button = static_cast<Uint8>(st.c); ev.button.x = st.a; ev.button.y = st.b; ev.button.state = st.k == Step::Down ? SDL_PRESSED : SDL_RELEASED; ev.button.clicks = 1; SDL_PushEvent(&ev); break;
                case Step::Key: ev.type = st.c ? SDL_KEYDOWN : SDL_KEYUP; ev.key.windowID = SDL_GetWindowID(window); ev.key.keysym.sym = st.a; ev.key.keysym.scancode = SDL_GetScancodeFromKey(st.a); ev.key.keysym.mod = static_cast<Uint16>(st.b); ev.key.state = st.c ? SDL_PRESSED : SDL_RELEASED; SDL_PushEvent(&ev); break;
                case Step::Text: ev.type = SDL_TEXTINPUT; ev.text.windowID = SDL_GetWindowID(window); std::snprintf(ev.text.text, sizeof ev.text.text, "%s", st.str.c_str()); SDL_PushEvent(&ev); break;
                case Step::Wait: script_wait = st.a; break;
                case Step::Shot: save_screenshot(window, st.str); break;
                }
                script_wait = std::max(script_wait, 2);   // let ImGui see each event on its own frame
            }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(window)))
                app.request_close_project([&app] { app.quit_requested = true; });
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(50); continue; }

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        app.now = ImGui::GetTime();

        // dockspace over the whole work area
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize); ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##dockhost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(3);
        const ImGuiID dock = ImGui::GetID("MelovioDock");
        if (!ImGui::DockBuilderGetNode(dock)) build_default_layout(dock, vp->WorkSize);
        ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();

        if (getenv("MELOVIO_DEBUG_INPUT") && script_pos > 0) std::fprintf(stderr, "f%d pos %.0f,%.0f down %d step %zu\n", frame, static_cast<double>(io.MousePos.x), static_cast<double>(io.MousePos.y), io.MouseDown[0] ? 1 : 0, script_pos);
        app.refresh_diagnostics();
        draw_main_menu(app);
        draw_transport_bar(app);
        draw_tracks(app); draw_voicebanks(app); draw_piano_roll(app); draw_param_lanes(app); draw_phonemes(app); draw_properties(app); draw_workshop(app);
        draw_dialogs(app);
        handle_global_shortcuts(app);
        if (!args.focus.empty() && frame == 3) { ImGui::SetWindowFocus(args.focus.c_str()); }

        // autosave (never overwrites the project file; see AutosaveStore)
        if (app.dirty && app.cfg.autosave_seconds > 0 && app.now - app.last_autosave >= app.cfg.autosave_seconds) {
            app.last_autosave = app.now;
            Status s = app.autosave->write(app.project, app.project_path);
            if (!s) app.add_log("autosave failed: " + s.message);
        }
        if (!app.dirty) app.last_autosave = app.now;
        const std::string title = app.window_title();
        if (title != last_title) { SDL_SetWindowTitle(window, title.c_str()); last_title = title; }

        ImGui::Render();
        int dw, dh; SDL_GL_GetDrawableSize(window, &dw, &dh);
        using ClearFn = void (*)(unsigned); using ViewportFn = void (*)(int, int, int, int); using ClearColorFn = void (*)(float, float, float, float);
        static auto gl_viewport = reinterpret_cast<ViewportFn>(SDL_GL_GetProcAddress("glViewport"));
        static auto gl_clear_color = reinterpret_cast<ClearColorFn>(SDL_GL_GetProcAddress("glClearColor"));
        static auto gl_clear = reinterpret_cast<ClearFn>(SDL_GL_GetProcAddress("glClear"));
        if (gl_viewport && gl_clear_color && gl_clear) { gl_viewport(0, 0, dw, dh); gl_clear_color(0.08f, 0.085f, 0.1f, 1.f); gl_clear(0x4000); }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frame;
        if (args.frames > 0 && frame >= args.frames) { if (!args.screenshot.empty()) save_screenshot(window, args.screenshot); running = false; }
        SDL_GL_SwapWindow(window);
    }

    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
