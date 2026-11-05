#include "test_util.hpp"
#include "core/config.hpp"
using namespace melovio; using namespace tu;

TEST_CASE("config: recent projects, persistence, corruption") {
    TempDir d; AppConfig c;
    for (int i = 0; i < 15; ++i) c.add_recent(d / ("p" + std::to_string(i) + ".mvp"));
    CHECK(c.recent_projects.size() == 12);
    c.add_recent(d / "p14.mvp"); CHECK(c.recent_projects.size() == 12);                            // no duplicates, most recent first
    CHECK(c.recent_projects[0] == path_to_utf8(fs::absolute(d / "p14.mvp")));
    CHECK(c.recent_existing().empty());                                                           // none of those files exist
    { std::ofstream(d / "real.mvp") << "x"; } c.add_recent(d / "real.mvp"); CHECK(c.recent_existing().size() == 1);
    c.autosave_seconds = 30; c.export_defaults.sample_rate = 48000; c.export_defaults.bit_depth = 24; c.voicebank_dirs = { "/x" };
    REQUIRE(save_config(c, d / "cfg" / "settings.json"));
    auto r = load_config(d / "cfg" / "settings.json"); REQUIRE(r);
    CHECK(r->autosave_seconds == 30); CHECK(r->export_defaults.sample_rate == 48000); CHECK(r->export_defaults.bit_depth == 24); CHECK(r->voicebank_dirs.size() == 1);
    CHECK(r->recent_projects == c.recent_projects);
    CHECK(load_config(d / "absent.json"));                                                         // missing file = defaults
    { std::ofstream(d / "bad.json") << "{{{"; }
    CHECK_FALSE(load_config(d / "bad.json"));
    std::string warn; auto def = load_config_or_default(d / "bad.json", &warn); CHECK_FALSE(warn.empty()); CHECK(def.autosave_seconds == 60);
}
