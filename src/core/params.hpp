// Registry of the vocal parameters Melovio exposes. Every entry maps onto something Wauvio-Vocal v2 actually
// renders (a Style field or an expression channel). Nothing is listed that the engine cannot do.
#pragma once
#include <string>
#include <vector>

namespace melovio {
    struct ParamDef {
        const char* id;          // stable key used in project files and the engine bridge
        const char* label;
        const char* group;       // "Voice", "Pitch", "Articulation", "Expression"
        float min, max, def;     // def = "not overridden" value shown in the UI (the voice's own default is used when unset)
        const char* unit;        // "", "st", "Hz", "s"
        bool note_param;         // can be set per note / per track (maps to a Wauvio Style field)
        bool lane;               // can be drawn as an automation lane (maps to a Wauvio expression channel; additive offset)
        const char* tip;
    };

    // Per-note / per-track Style overrides and automation lanes. Lane values are ADDITIVE offsets around 0
    // (that is how the engine's expression channels work); `pitch` is a semitone deviation curve.
    inline const std::vector<ParamDef>& param_registry() {
        static const std::vector<ParamDef> r = {
            // id               label            group          min   max   def   unit  note  lane  tip
            { "pitch",          "Pitch deviation","Pitch",      -12.f, 12.f, 0.f,  "st", false, true,  "Free pitch offset in semitones added on top of the notes (bends, scoops, fine tuning)." },
            { "volume",         "Volume",        "Expression",  -1.f,  1.f,  0.f,  "",   false, true,  "Gain offset: output is multiplied by (1 + value)." },
            { "brightness",     "Brightness",    "Voice",       -1.f,  1.f,  0.f,  "",   true,  true,  "High-frequency presence. Note override is absolute 0..1; lane is an additive offset." },
            { "breathiness",    "Breathiness",   "Voice",       -1.f,  1.f,  0.f,  "",   true,  true,  "Aspiration noise and more open glottis." },
            { "tension",        "Tension",       "Voice",       -1.f,  1.f,  0.f,  "",   true,  true,  "Glottal tenseness: higher is sharper and brighter." },
            { "formant_shift",  "Gender / formant", "Voice",    -12.f, 12.f, 0.f,  "st", true,  true,  "Moves the vocal tract formants without changing pitch." },
            { "mouth_open",     "Mouth opening", "Voice",       -1.f,  1.f,  0.f,  "",   true,  true,  "Jaw opening: raises or lowers the first formant." },
            { "growl",          "Growl",         "Expression",   0.f,  1.f,  0.f,  "",   true,  true,  "Sub-harmonic roughness." },
            { "tremor",         "Tremor",        "Expression",   0.f,  1.f,  0.f,  "",   true,  true,  "Shaky voice (pitch and amplitude)." },
            { "fry",            "Vocal fry",     "Expression",   0.f,  1.f,  0.f,  "",   true,  true,  "Creaky period-doubling." },
            { "air",            "Air",           "Expression",   0.f,  1.f,  0.f,  "",   true,  true,  "Extra unfiltered breath." },
            { "reg",            "Register",      "Voice",        0.f,  1.f,  0.f,  "",   true,  true,  "0 chest/modal .. 1 falsetto/head. (Lane id: register)" },
            { "softness",       "Softness",      "Voice",        0.f,  1.f,  0.f,  "",   true,  false, "Rounded onset and breathier tone." },
            { "power",          "Power",         "Voice",        0.f,  1.f,  0.5f, "",   true,  false, "Vocal effort: singer's formant and tenser source." },
            { "vibrato_depth",  "Vibrato depth", "Pitch",        0.f,  2.f,  0.3f, "st", true,  false, "Peak vibrato depth in semitones. 0 disables vibrato for the note." },
            { "vibrato_rate",   "Vibrato rate",  "Pitch",        2.f,  10.f, 5.7f, "Hz", true,  false, "Vibrato speed." },
            { "vibrato_delay",  "Vibrato delay", "Pitch",        0.f,  2.f,  0.3f, "s",  true,  false, "Time before vibrato starts." },
            { "portamento",     "Portamento",    "Pitch",        0.f,  0.4f, 0.06f,"s",  true,  false, "Glide time between connected notes." },
            { "scoop",          "Scoop",         "Pitch",        0.f,  2.f,  0.3f, "st", true,  false, "Slide up into the note from below after a rest." },
            { "consonant_strength","Consonant strength","Articulation",0.f,1.f,0.55f,"", true,  false, "Loudness of consonant noise and bursts." },
            { "vowel_clarity",  "Vowel clarity", "Articulation", 0.f,  1.f,  0.6f, "",   true,  false, "How cleanly vowels are articulated." },
            { "legato",         "Legato",        "Articulation", 0.f,  1.f,  0.5f, "",   true,  false, "1 = notes run together." },
        };
        return r;
    }
    inline const ParamDef* find_param(const std::string& id) {
        for (auto& p : param_registry()) if (id == p.id) return &p;
        if (id == "register") return find_param("reg");
        return nullptr;
    }
    // Map a lane/param id to the engine channel name used by Singer::lane ("register" for reg).
    inline std::string engine_lane_name(const std::string& id) { return id == "reg" ? "register" : id; }
}
