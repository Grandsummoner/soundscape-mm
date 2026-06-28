#include "plugin.hpp"

// ============================================================
// Scales (Voltage Block manual p.11) — ported verbatim from Skyline.cpp
// ============================================================
static const float SCALES[16][12] = {
    {0,1,2,3,4,5,6,7,8,9,10,11},  // 0  Unquantized
    {0,1,5,7,10,0,1,5,7,10,0,1},  // 1  Japanese (In)
    {0,2,4,7,9,0,2,4,7,9,0,2},    // 2  Major Pentatonic
    {0,3,5,7,10,0,3,5,7,10,0,3},  // 3  Minor Pentatonic
    {0,3,5,6,7,10,0,3,5,6,7,10},  // 4  Blues
    {0,1,3,4,6,8,10,0,1,3,4,6},   // 5  Locrian
    {0,2,4,5,6,8,10,0,2,4,5,6},   // 6  Arabian
    {0,1,3,5,7,8,10,0,1,3,5,7},   // 7  Phrygian
    {0,2,3,5,7,8,10,0,2,3,5,7},   // 8  Natural Minor
    {0,2,3,5,7,9,10,0,2,3,5,7},   // 9  Dorian
    {0,2,4,5,7,9,10,0,2,4,5,7},   // 10 Mixolydian
    {0,1,4,5,7,8,11,0,1,4,5,7},   // 11 Persian
    {0,1,4,5,7,8,11,0,1,4,5,7},   // 12 Double Harmonic
    {0,2,4,5,7,9,11,0,2,4,5,7},   // 13 Major
    {0,2,4,6,7,9,11,0,2,4,6,7},   // 14 Lydian
    {0,1,2,3,4,5,6,7,8,9,10,11},  // 15 Chromatic
};
static const int SCALE_SIZES[16] = {12,5,5,5,6,7,7,7,7,7,7,7,7,7,7,12};

static float quantizeVoltage(float v, int scaleIdx) {
    if (scaleIdx == 0 || scaleIdx == 15) return v;
    float semitones = v * 12.0f;
    int octave = (int)std::floor(semitones / 12.0f);
    int semi   = (int)std::floor(semitones) - octave * 12;
    if (semi < 0) { semi += 12; octave--; }
    int sz = SCALE_SIZES[scaleIdx];
    int best = 0, bestDist = 12;
    for (int i = 0; i < sz; i++) {
        int d = std::abs(semi - (int)SCALES[scaleIdx][i]);
        if (d < bestDist) { bestDist = d; best = (int)SCALES[scaleIdx][i]; }
    }
    return (octave * 12 + best) / 12.0f;
}

// Discrete ratio table shared by EXT's clock "gearbox" (Knob 1) and
// MOD's S&H rate (Knob 3) — both are described as multiplying/dividing
// a reference rate, so they reuse the same snapped ratio set.
static const float GEAR_RATIOS[7] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f};
static float gearRatioFromKnob(float k01) {
    int idx = clamp((int)std::round(k01 * 6.f), 0, 6);
    return GEAR_RATIOS[idx];
}

// ============================================================
struct Soundscape : Module {
// ============================================================
    enum ParamIds {
        ENGINE_MODE_PARAM,      // 3-way: INT / EXT / MOD
        KNOB1_PARAM,            // SYNC / CLOCK
        KNOB2_PARAM,            // OFFSET / BIAS
        KNOB3_PARAM,            // RHYTHM / DENSITY
        ENUMS(CH_ATTEN_PARAMS, 8),
        ENUMS(SLIDER_PARAMS, 8),
        ENUMS(DISPLAY_BTN_PARAMS, 8),  // per-channel 7-seg push-button (cycle C/P/G, select edit target)
        ENUMS(PERFORM_PARAMS, 8),      // CLEAR, SMOOTH, RND, FREEZE, FWD, REV, PEND, RNDSEQ
        NUM_PARAMS
    };
    enum InputIds  { CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };   // CLOCK_INPUT jack = "CLK/CV", RESET_INPUT jack = "RST/HLD"
    enum OutputIds { ENUMS(CV_OUTPUTS, 8), NUM_OUTPUTS };
    enum LightIds  {
        ENUMS(CHANNEL_LIGHTS,   8 * 3),  // RGB, output-level / mode-color indicator
        ENUMS(EDIT_RING_LIGHTS, 8),      // glow ring on whichever channel PERFORM buttons target
        ENUMS(PERFORM_LIGHTS,   8 * 3),  // feedback for FREEZE / SMOOTH / FWD-REV-PEND-RNDSEQ
        NUM_LIGHTS
    };

    enum EngineMode { ENGINE_INT, ENGINE_EXT, ENGINE_MOD };
    enum ChanMode   { CH_CV, CH_PITCH, CH_GATE };

    // ---- Per-channel sequencer memory (ported from Skyline) ----
    float stepCV[8][16]     = {};
    int   seqLength[8]      = {16,16,16,16,16,16,16,16};
    int   seqPos[8]         = {};
    bool  stepSmooth[8][16] = {};
    int   direction[8]      = {};              // 0 fwd, 1 rev, 2 pendulum, 3 random
    int   pendDir[8]        = {1,1,1,1,1,1,1,1};
    int   scaleIndex[8]     = {13,13,13,13,13,13,13,13}; // default Major; no per-channel scale UI yet (see note in widget ctor)
    bool  frozen[8]         = {};
    float glideCV[8]        = {};
    float prevSlider[8]     = {};

    // ---- New Soundscape-specific per-channel state ----
    int   chMode[8]   = {CH_CV,CH_CV,CH_CV,CH_CV,CH_CV,CH_CV,CH_CV,CH_CV};
    int   editChan     = 0;     // which channel the 8 PERFORM buttons currently target
    float glowPhase    = 0.f;
    float chTweakTimer[8] = {}; // counts down after CH_ATTEN knob is moved -> shows VU instead of C/P/G glyph
    float prevAtten[8]    = {-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f};

    // ---- Engine / clock state ----
    dsp::SchmittTrigger clockTrig, resetTrig;
    dsp::SchmittTrigger displayBtnTrig[8];
    dsp::SchmittTrigger performBtnTrig[8];
    float timeSinceLastClock = 0.f;
    float lastClockPeriod    = 0.5f;
    bool  wasHolding         = false;

    int   subStepCount[8] = {};   // INT-mode per-channel polyrhythm divisor counter
    float internalPhase   = 0.f;  // INT: master BPM phase  |  MOD: "division reference" grid phase
    float extGearPhase    = 0.f;  // EXT: gearboxed clock phase accumulator
    float extOffsetHeld   = 0.f;  // EXT: Knob2 value sampled-and-held at each real clock edge
    float modSHPhase       = 0.f; // MOD: S&H sub-phase, relative to internalPhase ticks

    Soundscape() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configSwitch(ENGINE_MODE_PARAM, 0.f, 2.f, 0.f, "Engine Mode", {"INT", "EXT", "MOD"});
        configParam(KNOB1_PARAM, 0.f, 1.f, 0.5f, "Knob 1: Sync / Clock");
        configParam(KNOB2_PARAM, 0.f, 1.f, 0.5f, "Knob 2: Offset / Bias");
        configParam(KNOB3_PARAM, 0.f, 1.f, 0.5f, "Knob 3: Rhythm / Density");

        for (int i = 0; i < 8; i++) {
            configParam(CH_ATTEN_PARAMS + i, 0.f, 1.f, 1.f, string::f("Ch %d Atten", i + 1));
            configParam(SLIDER_PARAMS + i, 0.f, 4.f, 0.f, string::f("Ch %d Slider", i + 1), " V");
            configButton(DISPLAY_BTN_PARAMS + i, string::f("Ch %d Mode / Select", i + 1));
            configOutput(CV_OUTPUTS + i, string::f("Ch %d CV", i + 1));
        }
        const char* performNames[8] = {"Clear","Smooth","Randomize","Freeze","Forward","Reverse","Pendulum","Random Seq"};
        for (int i = 0; i < 8; i++)
            configButton(PERFORM_PARAMS + i, performNames[i]);

        configInput(CLOCK_INPUT, "Clock / CV");
        configInput(RESET_INPUT, "Reset / Hold");
    }

    void advanceChannel(int ch) {
        int len = seqLength[ch];
        switch (direction[ch]) {
            case 0: seqPos[ch] = (seqPos[ch] + 1) % len; break;
            case 1: seqPos[ch] = (seqPos[ch] - 1 + len) % len; break;
            case 2:
                seqPos[ch] += pendDir[ch];
                if (seqPos[ch] >= len - 1) { seqPos[ch] = len - 1; pendDir[ch] = -1; }
                if (seqPos[ch] <= 0)       { seqPos[ch] = 0;       pendDir[ch] = 1; }
                break;
            case 3: seqPos[ch] = (int)(random::uniform() * len); break;
        }
    }

    void process(const ProcessArgs& args) override {
        int mode = (int)params[ENGINE_MODE_PARAM].getValue();
        float k1 = params[KNOB1_PARAM].getValue();
        float k2 = params[KNOB2_PARAM].getValue();
        float k3 = params[KNOB3_PARAM].getValue();

        // ---------------------------------------------------------
        // 1. Per-channel display button: cycle C/P/G, select edit target
        // ---------------------------------------------------------
        for (int ch = 0; ch < 8; ch++) {
            if (displayBtnTrig[ch].process(params[DISPLAY_BTN_PARAMS + ch].getValue())) {
                chMode[ch] = (chMode[ch] + 1) % 3;
                editChan = ch;
            }
        }

        // ---------------------------------------------------------
        // 2. Perform buttons act on the currently-selected (editChan) channel
        //    Mapping reuses Skyline's existing semantics verbatim.
        // ---------------------------------------------------------
        for (int i = 0; i < 8; i++) {
            if (!performBtnTrig[i].process(params[PERFORM_PARAMS + i].getValue())) continue;
            switch (i) {
                case 0: // CLEAR
                    for (int s = 0; s < 16; s++) stepCV[editChan][s] = 0.f;
                    seqPos[editChan] = 0;
                    break;
                case 1: // SMOOTH (toggle glide on the currently playing step)
                    stepSmooth[editChan][seqPos[editChan]] = !stepSmooth[editChan][seqPos[editChan]];
                    break;
                case 2: // RANDOMIZE this channel's memory
                    for (int s = 0; s < seqLength[editChan]; s++)
                        stepCV[editChan][s] = random::uniform() * 4.f;
                    break;
                case 3: frozen[editChan]    = !frozen[editChan]; break;  // FREEZE
                case 4: direction[editChan] = 0; break;                  // FWD
                case 5: direction[editChan] = 1; break;                  // REV
                case 6: direction[editChan] = 2; break;                  // PEND
                case 7: direction[editChan] = 3; break;                  // RNDSEQ
                default: break;
            }
        }

        // ---------------------------------------------------------
        // 3. Live fader recording — writes into the CURRENTLY PLAYING
        //    step of whichever channel's slider was just touched.
        // ---------------------------------------------------------
        for (int ch = 0; ch < 8; ch++) {
            float sv = params[SLIDER_PARAMS + ch].getValue();
            if (std::abs(sv - prevSlider[ch]) > 0.0001f) {
                stepCV[ch][seqPos[ch]] = sv;
                prevSlider[ch] = sv;
            }
        }

        // ---------------------------------------------------------
        // 4. Reset / Hold jack
        //    Rising edge -> reset all channels to step 0.
        //    Sustained high -> pause clocking (no advance) in any mode.
        // ---------------------------------------------------------
        bool holding = inputs[RESET_INPUT].getVoltage() > 2.f;
        if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
            for (int ch = 0; ch < 8; ch++) seqPos[ch] = 0;
            for (int ch = 0; ch < 8; ch++) subStepCount[ch] = 0;
            internalPhase = 0.f; extGearPhase = 0.f; modSHPhase = 0.f;
        }
        wasHolding = holding;

        // ---------------------------------------------------------
        // 5. Mode-dependent clock / sample generation
        // ---------------------------------------------------------
        timeSinceLastClock += args.sampleTime;
        bool realClockEdge = clockTrig.process(inputs[CLOCK_INPUT].getVoltage());
        if (realClockEdge) {
            if (timeSinceLastClock > 0.001f && timeSinceLastClock < 10.f)
                lastClockPeriod = timeSinceLastClock;
            timeSinceLastClock = 0.f;
        }

        if (!holding) {
            if (mode == ENGINE_INT) {
                // Knob 1: master BPM. Knob 3: per-channel divisor spread (polyrhythm).
                float bpm = 20.f + k1 * 280.f;
                float stepPeriod = 60.f / (bpm * 4.f); // 16th-note resolution
                internalPhase += args.sampleTime;
                if (internalPhase >= stepPeriod) {
                    internalPhase -= stepPeriod;
                    for (int ch = 0; ch < 8; ch++) {
                        if (frozen[ch]) continue;
                        // divisor: ch0 always 1 (unison); ch7 up to 8 at k3=1 (full spread)
                        int divisor = 1 + (int)std::round(k3 * ch);
                        if (++subStepCount[ch] >= divisor) {
                            subStepCount[ch] = 0;
                            advanceChannel(ch);
                        }
                    }
                }
            }
            else if (mode == ENGINE_EXT) {
                // Knob 1: clock "gearbox" multiplier/divider on the incoming clock.
                // Knob 2: sampled-and-held at each real clock edge (clock-aligned, glitch-free).
                // Knob 3: per-channel independent probability that a tick actually advances.
                if (realClockEdge) {
                    extOffsetHeld = (k2 - 0.5f) * 10.f; // -5..+5V
                    extGearPhase = 0.f;                  // resync gearbox to the real edge
                }
                float ratio = gearRatioFromKnob(k1);
                float gearPeriod = lastClockPeriod / std::max(ratio, 0.01f);
                extGearPhase += args.sampleTime;
                if (extGearPhase >= gearPeriod) {
                    extGearPhase -= gearPeriod;
                    for (int ch = 0; ch < 8; ch++) {
                        if (frozen[ch]) continue;
                        if (random::uniform() <= k3) advanceChannel(ch);
                    }
                }
            }
            else { // ENGINE_MOD
                // Knob 1: internal "division reference" grid (same shape as INT's BPM).
                // Knob 2: recenters the incoming CV (CLOCK_INPUT used as continuous CV here).
                // Knob 3: S&H rate, expressed as a ratio of Knob 1's grid.
                float bpm = 20.f + k1 * 280.f;
                float gridPeriod = 60.f / (bpm * 4.f);
                float ratio = gearRatioFromKnob(k3);
                float shPeriod = gridPeriod / std::max(ratio, 0.01f);

                internalPhase += args.sampleTime;
                modSHPhase     += args.sampleTime;
                if (internalPhase >= gridPeriod) internalPhase -= gridPeriod;
                if (modSHPhase >= shPeriod) {
                    modSHPhase -= shPeriod;
                    float biasedCV = inputs[CLOCK_INPUT].getVoltage() + (k2 - 0.5f) * 10.f;
                    biasedCV = clamp(biasedCV, -5.f, 10.f);
                    for (int ch = 0; ch < 8; ch++) {
                        if (frozen[ch]) continue;
                        stepCV[ch][seqPos[ch]] = biasedCV; // sample now...
                        advanceChannel(ch);                 // ...then step to the next memory slot
                    }
                }
            }
        }

        // ---------------------------------------------------------
        // 6. Per-channel attenuator "tweak" detection (for VU overlay)
        // ---------------------------------------------------------
        for (int ch = 0; ch < 8; ch++) {
            float a = params[CH_ATTEN_PARAMS + ch].getValue();
            if (prevAtten[ch] >= 0.f && std::abs(a - prevAtten[ch]) > 0.001f)
                chTweakTimer[ch] = 0.5f;
            prevAtten[ch] = a;
            if (chTweakTimer[ch] > 0.f) chTweakTimer[ch] -= args.sampleTime;
        }

        // ---------------------------------------------------------
        // 7. Output stage — same pipeline regardless of mode
        // ---------------------------------------------------------
        for (int ch = 0; ch < 8; ch++) {
            float raw = stepCV[ch][seqPos[ch]];
            float atten = params[CH_ATTEN_PARAMS + ch].getValue();
            float v = raw * atten;

            if (mode == ENGINE_INT)      v += (k2 - 0.5f) * 10.f; // continuous DC shift
            else if (mode == ENGINE_EXT) v += extOffsetHeld;       // clock-aligned held offset
            // ENGINE_MOD: bias already applied to the sampled CV before it was stored

            if (stepSmooth[ch][seqPos[ch]]) {
                float glideTime = std::max(lastClockPeriod * 0.9f, 0.05f);
                float rate = 1.0f / (args.sampleRate * glideTime);
                glideCV[ch] += (v - glideCV[ch]) * rate;
                v = glideCV[ch];
            } else {
                glideCV[ch] = v;
            }

            float outVal;
            switch (chMode[ch]) {
                case CH_PITCH: outVal = quantizeVoltage(v / 4.0f, scaleIndex[ch]) * 4.0f; break;
                case CH_GATE:  outVal = (v > 2.0f) ? 10.f : 0.f; break;
                default:       outVal = v; break; // CH_CV
            }
            outVal = clamp(outVal, -5.f, 10.f);
            outputs[CV_OUTPUTS + ch].setVoltage(outVal);

            float bright = clamp(std::abs(outVal) / 10.f, 0.f, 1.f);
            float r = (chMode[ch] == CH_GATE)  ? bright : 0.f;
            float g = (chMode[ch] == CH_PITCH) ? bright : 0.f;
            float b = (chMode[ch] == CH_CV)    ? bright : 0.f;
            lights[CHANNEL_LIGHTS + ch * 3 + 0].setBrightness(r);
            lights[CHANNEL_LIGHTS + ch * 3 + 1].setBrightness(g);
            lights[CHANNEL_LIGHTS + ch * 3 + 2].setBrightness(b);
        }

        // ---------------------------------------------------------
        // 8. Perform-button feedback lights
        // ---------------------------------------------------------
        for (int i = 0; i < 4; i++)
            lights[PERFORM_LIGHTS + (4 + i) * 3].setBrightness((direction[editChan] == i) ? 1.f : 0.f);
        lights[PERFORM_LIGHTS + 1 * 3].setBrightness(stepSmooth[editChan][seqPos[editChan]] ? 1.f : 0.f);
        lights[PERFORM_LIGHTS + 3 * 3].setBrightness(frozen[editChan] ? 1.f : 0.f);

        // ---------------------------------------------------------
        // 9. Edit-target glow ring
        // ---------------------------------------------------------
        glowPhase += args.sampleTime * 1.5f;
        if (glowPhase > 2.f * (float)M_PI) glowPhase -= 2.f * (float)M_PI;
        float glow = 0.45f + 0.45f * std::sin(glowPhase);
        for (int ch = 0; ch < 8; ch++)
            lights[EDIT_RING_LIGHTS + ch].setBrightness(ch == editChan ? glow : 0.f);
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        auto arrF = [&](const char* key, std::function<void(json_t*)> fn) {
            json_t* a = json_array(); fn(a); json_object_set_new(root, key, a);
        };
        arrF("stepCV", [&](json_t* a) {
            for (int ch = 0; ch < 8; ch++) for (int s = 0; s < 16; s++)
                json_array_append_new(a, json_real(stepCV[ch][s]));
        });
        arrF("stepSmooth", [&](json_t* a) {
            for (int ch = 0; ch < 8; ch++) for (int s = 0; s < 16; s++)
                json_array_append_new(a, json_boolean(stepSmooth[ch][s]));
        });
        arrF("direction",  [&](json_t* a){ for (int ch=0; ch<8; ch++) json_array_append_new(a, json_integer(direction[ch])); });
        arrF("pendDir",    [&](json_t* a){ for (int ch=0; ch<8; ch++) json_array_append_new(a, json_integer(pendDir[ch])); });
        arrF("scaleIndex", [&](json_t* a){ for (int ch=0; ch<8; ch++) json_array_append_new(a, json_integer(scaleIndex[ch])); });
        arrF("frozen",     [&](json_t* a){ for (int ch=0; ch<8; ch++) json_array_append_new(a, json_boolean(frozen[ch])); });
        arrF("chMode",     [&](json_t* a){ for (int ch=0; ch<8; ch++) json_array_append_new(a, json_integer(chMode[ch])); });
        json_object_set_new(root, "editChan", json_integer(editChan));
        return root;
    }

    void dataFromJson(json_t* root) override {
        auto getI = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k); if (!a) return 0;
            json_t* v = json_array_get(a, idx); return v ? (int)json_integer_value(v) : 0;
        };
        auto getF = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k); if (!a) return 0.f;
            json_t* v = json_array_get(a, idx); return v ? (float)json_real_value(v) : 0.f;
        };
        auto getB = [&](const char* k, int idx) {
            json_t* a = json_object_get(root, k); if (!a) return false;
            json_t* v = json_array_get(a, idx); return v ? json_boolean_value(v) : false;
        };
        int idx = 0;
        for (int ch = 0; ch < 8; ch++) for (int s = 0; s < 16; s++) stepCV[ch][s] = getF("stepCV", idx++);
        idx = 0;
        for (int ch = 0; ch < 8; ch++) for (int s = 0; s < 16; s++) stepSmooth[ch][s] = getB("stepSmooth", idx++);
        for (int ch = 0; ch < 8; ch++) direction[ch]  = getI("direction", ch);
        for (int ch = 0; ch < 8; ch++) pendDir[ch]    = getI("pendDir", ch);
        for (int ch = 0; ch < 8; ch++) scaleIndex[ch] = getI("scaleIndex", ch);
        for (int ch = 0; ch < 8; ch++) frozen[ch]     = getB("frozen", ch);
        for (int ch = 0; ch < 8; ch++) chMode[ch]     = getI("chMode", ch);
        json_t* ec = json_object_get(root, "editChan");
        if (ec) editChan = (int)json_integer_value(ec);
    }

    void onRandomize(const RandomizeEvent& e) override {
        for (int ch = 0; ch < 8; ch++) for (int s = 0; s < 16; s++) stepCV[ch][s] = random::uniform() * 4.f;
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++) { stepCV[ch][s] = 0.f; stepSmooth[ch][s] = false; }
            seqLength[ch] = 16; seqPos[ch] = 0; direction[ch] = 0; pendDir[ch] = 1;
            scaleIndex[ch] = 13; frozen[ch] = false; glideCV[ch] = 0.f; chMode[ch] = CH_CV;
            subStepCount[ch] = 0;
        }
        editChan = 0; glowPhase = 0.f;
        internalPhase = 0.f; extGearPhase = 0.f; extOffsetHeld = 0.f; modSHPhase = 0.f;
    }
};

// ============================================================
// EditRingLight — ported verbatim from Skyline.cpp. Glowing ring
// drawn around a channel LED, showing the current PERFORM-button
// edit target. Not a Param/Jack/Light subclass, so its NanoVG draw
// isn't substituted by the MetaModule engine's native rendering.
// ============================================================
struct EditRingLight : widget::Widget {
    int     lightId = 0;
    Module* mod      = nullptr;

    EditRingLight() { box.size = Vec(22, 22); }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;
        if (!mod) return;
        float brightness = mod->lights[lightId].getBrightness();
        if (brightness <= 0.001f) return;

        Vec centre = box.size.div(2.f);
        float r = 9.5f;
        const float NR = 0.1f, NG = 0.25f, NB = 0.6f;

        NVGpaint glow = nvgRadialGradient(args.vg,
            centre.x, centre.y, r * 0.7f, r * 1.6f,
            nvgRGBAf(NR, NG, NB, brightness * 0.55f),
            nvgRGBAf(NR, NG, NB, 0.f));
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, centre.x, centre.y, r * 1.6f);
        nvgFillPaint(args.vg, glow);
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgCircle(args.vg, centre.x, centre.y, r);
        nvgStrokeColor(args.vg, nvgRGBAf(NR, NG, NB, brightness * 0.9f));
        nvgStrokeWidth(args.vg, 1.8f);
        nvgStroke(args.vg);
    }
};

// ============================================================
// SoundscapeDisplay — sits over each channel's CV output. Shows the
// channel's C / P / G mode glyph when idle, or a momentary green-to-red
// VU bar (driven by chTweakTimer) when that channel's atten knob was
// just moved.
// ============================================================
struct SoundscapeDisplay : LightWidget {
    Soundscape* module    = nullptr;
    int         channelId = 0;

    SoundscapeDisplay() { box.size = Vec(24, 32); }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGBA(0x1a, 0x1a, 0x1a, 0xff));
        nvgFill(args.vg);
        if (!module) return;

        bool tweaking = module->chTweakTimer[channelId] > 0.f;

        if (tweaking) {
            float strength = module->params[Soundscape::CH_ATTEN_PARAMS + channelId].getValue();
            int bars = (int)std::round(strength * 4.f) + 1;
            for (int b = 0; b < bars; b++) {
                float t = (float)b / 4.f; // green (0) -> red (1)
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 4, 26 - (b * 6), 16, 4);
                nvgFillColor(args.vg, nvgRGBA((int)(t * 255), (int)((1.f - t) * 200), 0, 255));
                nvgFill(args.vg);
            }
        } else {
            nvgFontSize(args.vg, 20.f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff));
            const char* glyph = "C";
            if (module->chMode[channelId] == Soundscape::CH_PITCH) glyph = "P";
            else if (module->chMode[channelId] == Soundscape::CH_GATE) glyph = "G";
            nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, glyph, nullptr);
        }
    }
};

// ============================================================
// Widget
// ============================================================
// SoundscapePort already exists in res/SoundscapeJack.svg — reusing the
// same fix Skyline needed (asset::plugin, not asset::system, since the
// latter doesn't exist on the MetaModule firmware).
struct SoundscapePort : app::SvgPort {
    SoundscapePort() {
        setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SoundscapeJack.svg")));
    }
};

// SoundscapeSlider — ported verbatim from Skyline's SlimFader, renamed
// and pointed at the SoundscapeFaderBg/Handle SVGs already in res/.
struct SoundscapeSlider : app::SvgSlider {
    static const int TW = 6, TH = 42, HW = 14, HH = 8, TM = 6;
    bool  dragging     = false;
    float dragStartVal = 0.f;

    SoundscapeSlider() {
        setBackgroundSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SoundscapeFaderBg.svg")));
        setHandleSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SoundscapeFaderHandle.svg")));

        background->box.size = Vec(TW, TH);
        background->box.pos  = Vec((HW - TW) / 2.f, 0.f);
        handle->box.size     = Vec(HW, HH);
        setHandlePosCentered(
            Vec(HW / 2.f, TH - HH / 2.f), // value 0 -> bottom
            Vec(HW / 2.f, TM + HH / 2.f)  // value 1 -> top
        );
        box.size = Vec(HW, TH + HH);

        fb->box.size = box.size;
        fb->setDirty();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            dragging = true;
            dragStartVal = 0.f;
            if (getParamQuantity()) {
                float val = getParamQuantity()->getValue();
                float min = getParamQuantity()->getMinValue();
                float max = getParamQuantity()->getMaxValue();
                if (max > min) dragStartVal = (val - min) / (max - min);
            }
            e.consume(this);
        }
        if (e.action == GLFW_RELEASE) dragging = false;
        ParamWidget::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) APP->window->cursorLock();
        ParamWidget::onDragStart(e);
    }
    void onDragEnd(const DragEndEvent& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT) APP->window->cursorUnlock();
        ParamWidget::onDragEnd(e);
    }
    void onDragMove(const DragMoveEvent& e) override {
        if (!dragging || !getParamQuantity()) return;
        float sensitivity = (APP->window->getMods() & RACK_MOD_CTRL) ? 240.f : 60.f;
        float delta = -e.mouseDelta.y / sensitivity;
        dragStartVal = clamp(dragStartVal + delta, 0.f, 1.f);
        float min = getParamQuantity()->getMinValue();
        float max = getParamQuantity()->getMaxValue();
        getParamQuantity()->setValue(min + dragStartVal * (max - min));
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        if (getParamQuantity()) getParamQuantity()->reset();
    }
};

struct SoundscapeWidget : ModuleWidget {
    SoundscapeWidget(Soundscape* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Soundscape.svg")));

        // ---- Top control cluster ----
        const float xClk = 10.f, xRst = 22.f, xSwitch = 34.f;
        const float xK1 = 50.f, xK2 = 64.f, xK3 = 78.f;
        const float yTop = 24.f;

        addInput(createInputCentered<SoundscapePort>(mm2px(Vec(xClk, yTop)), module, Soundscape::CLOCK_INPUT));
        addInput(createInputCentered<SoundscapePort>(mm2px(Vec(xRst, yTop)), module, Soundscape::RESET_INPUT));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(xSwitch, yTop)), module, Soundscape::ENGINE_MODE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK1, yTop)), module, Soundscape::KNOB1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK2, yTop)), module, Soundscape::KNOB2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK3, yTop)), module, Soundscape::KNOB3_PARAM));

        // ---- Per-channel columns ----
        const float startX = 14.f, stepX = 16.f;
        const float yKnob = 40.f, ySld = 48.f, yDisplay = 92.f, yLed = 100.f, yOut = 108.f, yPerform = 118.f;

        for (int ch = 0; ch < 8; ch++) {
            float cx = startX + ch * stepX;

            addParam(createParamCentered<Trimpot>(mm2px(Vec(cx, yKnob)), module, Soundscape::CH_ATTEN_PARAMS + ch));

            addParam(createParam<SoundscapeSlider>(mm2px(Vec(cx - 2.37f, ySld)), module, Soundscape::SLIDER_PARAMS + ch));

            auto* disp = createWidget<SoundscapeDisplay>(mm2px(Vec(cx - 4.f, yDisplay - 4.f)));
            disp->module = module;
            disp->channelId = ch;
            addChild(disp);
            addParam(createParamCentered<VCVButton>(mm2px(Vec(cx, yDisplay + 6.f)), module, Soundscape::DISPLAY_BTN_PARAMS + ch));

            auto* ring = new EditRingLight;
            ring->mod = module;
            ring->lightId = Soundscape::EDIT_RING_LIGHTS + ch;
            ring->box.pos = mm2px(Vec(cx, yLed)).minus(ring->box.size.div(2.f));
            addChild(ring);
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(cx, yLed)), module, Soundscape::CHANNEL_LIGHTS + ch * 3));

            addOutput(createOutputCentered<SoundscapePort>(mm2px(Vec(cx, yOut)), module, Soundscape::CV_OUTPUTS + ch));

            addParam(createParamCentered<VCVButton>(mm2px(Vec(cx, yPerform)), module, Soundscape::PERFORM_PARAMS + ch));
            addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(cx, yPerform)), module, Soundscape::PERFORM_LIGHTS + ch * 3));
        }

        // NOTE: per-channel scaleIndex (used only in Pitch/"P" mode) has no
        // dedicated UI control yet — it defaults to Major for every channel.
        // A natural next step is a right-click context menu on each
        // SoundscapeDisplay to pick that channel's scale.
    }
};

Model* modelSoundscape = createModel<Soundscape, SoundscapeWidget>("SoundscapeMM");
