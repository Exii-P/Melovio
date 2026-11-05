#include "app.hpp"
#include <algorithm>
#include <cstring>

namespace melovio::ui {
    void FileDialog::show(const std::string& id_, const std::string& title_, FileMode m, std::vector<std::string> exts_, const fs::path& start_dir,
                          const std::string& default_name, std::function<void(const fs::path&)> cb) {
        id = id_; title = title_; mode = m; exts = std::move(exts_); on_pick = std::move(cb); error.clear();
        std::error_code ec;
        dir = (!start_dir.empty() && fs::is_directory(start_dir, ec)) ? start_dir : fs::current_path(ec);
        std::snprintf(name, sizeof name, "%s", default_name.c_str());
        request_open = true; open = true;
    }

    static bool ext_ok(const fs::path& p, const std::vector<std::string>& exts) {
        if (exts.empty()) return true;
        std::string e = path_to_utf8(p.extension()); for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return std::find(exts.begin(), exts.end(), e) != exts.end();
    }

    void FileDialog::draw() {
        if (!open) return;
        const std::string wid = title + "###filedlg_" + id;
        if (request_open) { ImGui::OpenPopup(wid.c_str()); request_open = false; }
        ImGui::SetNextWindowSize(ImVec2(640, 440), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        bool still = true;
        if (!ImGui::BeginPopupModal(wid.c_str(), &still, ImGuiWindowFlags_NoSavedSettings)) { if (!ImGui::IsPopupOpen(wid.c_str())) open = false; return; }
        char pathbuf[1024]; std::snprintf(pathbuf, sizeof pathbuf, "%s", path_to_utf8(dir).c_str());
        if (ImGui::Button("Up")) { if (dir.has_parent_path() && dir.parent_path() != dir) dir = dir.parent_path(); }
        ImGui::SameLine(); if (ImGui::Button("Home")) { if (const char* h = std::getenv("HOME")) dir = h; else if (const char* u = std::getenv("USERPROFILE")) dir = u; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", pathbuf, sizeof pathbuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec; fs::path np = path_from_utf8(pathbuf); if (fs::is_directory(np, ec)) dir = np; else error = "not a folder";
        }
        std::vector<fs::path> dirs, files; std::error_code ec;
        for (auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            const std::string fn = path_to_utf8(e.path().filename());
            if (!fn.empty() && fn[0] == '.') continue;
            if (e.is_directory(ec)) dirs.push_back(e.path()); else if (mode != FileMode::Folder && ext_ok(e.path(), exts)) files.push_back(e.path());
        }
        std::sort(dirs.begin(), dirs.end()); std::sort(files.begin(), files.end());
        ImGui::BeginChild("##list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * (mode == FileMode::Folder ? 2.f : 3.f)), true);
        for (auto& d : dirs) {
            const std::string label = "[ ] " + path_to_utf8(d.filename());
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) { dir = d; break; }
                if (mode == FileMode::Folder) std::snprintf(name, sizeof name, "%s", path_to_utf8(d.filename()).c_str());
            }
        }
        for (auto& f : files) {
            const std::string label = path_to_utf8(f.filename());
            if (ImGui::Selectable(label.c_str(), label == name, ImGuiSelectableFlags_AllowDoubleClick)) {
                std::snprintf(name, sizeof name, "%s", label.c_str());
                if (ImGui::IsMouseDoubleClicked(0) && mode == FileMode::Open) { fs::path pick = f; open = false; ImGui::CloseCurrentPopup(); ImGui::EndChild(); ImGui::EndPopup(); if (on_pick) on_pick(pick); return; }
            }
        }
        ImGui::EndChild();
        if (mode != FileMode::Folder) { ImGui::SetNextItemWidth(-1); ImGui::InputText("##name", name, sizeof name); }
        if (!error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error.c_str());
        const char* ok = mode == FileMode::Save ? "Save" : mode == FileMode::Folder ? "Choose this folder" : "Open";
        if (ImGui::Button(ok, ImVec2(160, 0))) {
            fs::path pick;
            if (mode == FileMode::Folder) { pick = dir; if (name[0]) { std::error_code e2; fs::path sub = dir / path_from_utf8(name); if (fs::is_directory(sub, e2)) pick = sub; } }
            else if (!name[0]) error = "enter a file name";
            else {
                pick = dir / path_from_utf8(name);
                if (mode == FileMode::Save && !exts.empty() && !ext_ok(pick, exts)) pick += exts[0];
                if (mode == FileMode::Open && !fs::exists(pick, ec)) { error = "file does not exist"; pick.clear(); }
            }
            if (!pick.empty()) { open = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); if (on_pick) on_pick(pick); return; }
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(100, 0)) || !still) { open = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}
