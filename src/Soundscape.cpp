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
// MOD's S&H rate (Knob 3) — both multiply/divide a reference rate.
static const float GEAR_RATIOS[7] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f};
static float gearRatioFromKnob(float k01) {
    int idx = clamp((int)std::round(k01 * 6.f), 0, 6);
    return GEAR_RATIOS[idx];
}

// ============================================================
struct Soundscape : Module {
// ============================================================
    enum ParamIds {
        ENGINE_MODE_PARAM,     // 3-way: INT / EXT / MOD  (was CLK_SWITCH_PARAM)
        KNOB1_PARAM,           // SYNC   — double duty per engine mode (was OFFSET_PARAM)
        KNOB2_PARAM,           // OFFSET — double duty per engine mode (was ATTENUATE_PARAM)
        KNOB3_PARAM,           // RHYTHM — double duty per engine mode (was DIVIDE_PARAM)
        MUTE_PARAM, LENGTH_PARAM, SHIFT_PARAM, SCALE_PARAM, SAVE_PARAM, RECALL_PARAM,
        ENUMS(SLIDER_PARAMS, 8),
        ENUMS(STEP_PARAMS, 16),
        ENUMS(CHMODE_PARAMS, 8),  // NEW: per-channel 7-seg combo button (cycles CV/Pitch/Gate)
        NUM_PARAMS
    };
    enum InputIds  { CLOCK_INPUT, RESET_INPUT, NUM_INPUTS };  // CLOCK_INPUT="CLK/CV", RESET_INPUT="RST/HLD"
    enum OutputIds { ENUMS(CV_OUTPUTS, 8), NUM_OUTPUTS };
    enum LightIds  {
        ENUMS(STEP_LIGHTS,     16*3),  // playhead (RGB)
        ENUMS(BUTTON_LIGHTS,   16*3),  // button LED (RGB)
        ENUMS(CHANNEL_LIGHTS,   8*3),  // channel output LED (RGB)
        ENUMS(EDIT_RING_LIGHTS,  8),   // glow ring showing the MUTE/LENGTH/SCALE/SHIFT target channel
        ENUMS(MUTE_LIGHT,   3),
        ENUMS(LENGTH_LIGHT, 3),
        ENUMS(SHIFT_LIGHT,  3),
        ENUMS(SCALE_LIGHT,  3),
        ENUMS(SAVE_LIGHT,   3),
        ENUMS(RECALL_LIGHT, 3),
        NUM_LIGHTS
    };

    enum EngineMode { ENGINE_INT, ENGINE_EXT, ENGINE_MOD };
    enum ChanMode   { CH_CV, CH_PITCH, CH_GATE };

    void setRGB(int baseId, float r, float g, float b) {
        lights[baseId + 0].setBrightness(r);
        lights[baseId + 1].setBrightness(g);
        lights[baseId + 2].setBrightness(b);
    }
    void clearRGB(int baseId) { setRGB(baseId, 0.f, 0.f, 0.f); }

    struct SaveAnimation {
        bool  active    = false;
        int   slot      = -1;
        float timer     = 0.f;
        float duration  = 0.8f;
        bool  isRecall  = false;
        void trigger(int sl, float dur, bool rec) { active=true; slot=sl; timer=0.f; duration=dur; isRecall=rec; }
    };
    SaveAnimation saveAnim;

    // ---- Sequencer memory — ported verbatim from Skyline ----
    float stepCV[8][16]     = {};
    int   seqLength[8]      = {16,16,16,16,16,16,16,16};
    int   seqPos[8]         = {};
    bool  stepMuted[8][16]  = {};
    bool  chanMuted[8]      = {};
    bool  stepSmooth[8][16] = {};
    int   direction[8]      = {};
    int   pendDir[8]        = {1,1,1,1,1,1,1,1};
    int   scaleIndex[8]     = {};
    bool  frozen[8]         = {};
    int   selectedChan      = 0;
    int   editChan          = 0;
    int   globalStep        = -1;
    float glowPhase         = 0.f;

    float lengthSliderSnapshot[8] = {-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f};
    float scaleSliderSnapshot     = -1.f;
    int   prevSelectedChan        = 0;
    static constexpr float LENGTH_DEADBAND = 0.15f;
    static constexpr float SCALE_DEADBAND  = 0.15f;

    float presetCV[16][8][16] = {};
    int   presetLen[16][8]    = {};
    int   presetScale[16][8]  = {};
    int   presetDir[16][8]    = {};
    bool  presetValid[16]     = {};
    float presetKnob3[16]     = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,
                                  0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f,0.5f}; // was presetDivide[16]

    bool muteMode=false, lengthMode=false, shiftMode=false;
    bool scaleMode=false, saveMode=false,  recallMode=false;
    bool prevMuteMode=false, prevLengthMode=false, prevShiftMode=false;
    bool prevScaleMode=false, prevSaveMode=false,  prevRecallMode=false;

    dsp::SchmittTrigger clockTrig, resetTrig, stepTrig[16];
    float glideCV[8]     = {};
    float prevSlider[8]  = {0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f};
    int   lastSeqPos[8]  = {};
    // Per-channel (was a single shared float in OG Skyline) because INT's
    // polyrhythm divisor and EXT's probability skip mean different channels
    // can now advance at different moments, not all on one shared tick.
    float stepHoldTimer[8] = {};
    static constexpr float STEP_HOLD_WINDOW = 0.08f;
    float timeSinceLastClock = 0.f;
    float lastClockPeriod    = 0.5f;

    // ---- New: engine-mode dispatch state (replaces the old clkMode/divCount) ----
    int   subStepCount[8] = {};   // INT: per-channel polyrhythm divisor counter
    float internalPhase   = 0.f;  // INT: master BPM phase  |  MOD: "division reference" grid phase
    float extGearPhase    = 0.f;  // EXT: gearboxed clock phase accumulator
    float extOffsetHeld   = 0.f;  // EXT: Knob2 sampled-and-held at each real clock edge
    float modSHPhase      = 0.f;  // MOD: S&H sub-phase relative to internalPhase
    float chTweakTimer[8] = {};   // momentary VU-bar timer, triggered by moving that channel's slider

    Soundscape() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configSwitch(ENGINE_MODE_PARAM, 0.f, 2.f, 0.f, "Engine Mode", {"INT", "EXT", "MOD"});
        configParam(KNOB1_PARAM, 0.f, 1.f, 0.5f, "Knob 1: Sync / Clock");
        configParam(KNOB2_PARAM, 0.f, 1.f, 0.5f, "Knob 2: Offset / Bias");
        configParam(KNOB3_PARAM, 0.f, 1.f, 0.5f, "Knob 3: Rhythm / Density");

        configButton(MUTE_PARAM,  "Mute");
        configButton(LENGTH_PARAM,"Length");
        configButton(SHIFT_PARAM, "Shift");
        configButton(SCALE_PARAM, "Scale");
        configButton(SAVE_PARAM,  "Save");
        configButton(RECALL_PARAM,"Recall");

        for (int i = 0; i < 8; i++) {
            configParam(SLIDER_PARAMS + i, 0.f, 4.f, 0.f, string::f("Ch %d Slider", i + 1), " V");
            configOutput(CV_OUTPUTS + i, string::f("Ch %d CV", i + 1));
            configSwitch(CHMODE_PARAMS + i, 0.f, 2.f, 0.f, string::f("Ch %d Mode", i + 1), {"CV", "Pitch", "Gate"});
        }
        for (int i = 0; i < 16; i++)
            configButton(STEP_PARAMS + i, string::f("Step %d", i + 1));

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

    void savePreset(int slot) {
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++) presetCV[slot][ch][s] = stepCV[ch][s];
            presetLen[slot][ch]   = seqLength[ch];
            presetScale[slot][ch] = scaleIndex[ch];
            presetDir[slot][ch]   = direction[ch];
        }
        presetKnob3[slot] = params[KNOB3_PARAM].getValue();
        presetValid[slot] = true;
    }
    void recallPreset(int slot) {
        if (!presetValid[slot]) return;
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++) stepCV[ch][s] = presetCV[slot][ch][s];
            seqLength[ch]  = presetLen[slot][ch];
            scaleIndex[ch] = presetScale[slot][ch];
            direction[ch]  = presetDir[slot][ch];
            if (seqPos[ch] >= seqLength[ch]) seqPos[ch] = seqLength[ch] - 1;
        }
        params[KNOB3_PARAM].setValue(presetKnob3[slot]);
        for (int ch = 0; ch < 8; ch++) subStepCount[ch] = 0;
        internalPhase = 0.f; extGearPhase = 0.f; modSHPhase = 0.f;
    }

    void process(const ProcessArgs& args) override {
        // -----------------------------------------------------------
        // Latch-button submodes — ported verbatim from Skyline.
        // -----------------------------------------------------------
        bool rawMute   = params[MUTE_PARAM].getValue()   > 0.5f;
        bool rawLength = params[LENGTH_PARAM].getValue()  > 0.5f;
        bool rawShift  = params[SHIFT_PARAM].getValue()   > 0.5f;
        bool rawScale  = params[SCALE_PARAM].getValue()   > 0.5f;
        bool rawSave   = params[SAVE_PARAM].getValue()    > 0.5f;
        bool rawRecall = params[RECALL_PARAM].getValue()  > 0.5f;

        bool muteTrig   = rawMute   && !prevMuteMode;
        bool lengthTrig = rawLength && !prevLengthMode;
        bool shiftTrig  = rawShift  && !prevShiftMode;
        bool scaleTrig  = rawScale  && !prevScaleMode;
        bool saveTrig   = rawSave   && !prevSaveMode;
        bool recallTrig = rawRecall && !prevRecallMode;

        if (muteTrig || lengthTrig || shiftTrig || scaleTrig || saveTrig || recallTrig) {
            if (!muteTrig)   params[MUTE_PARAM].setValue(0.f);
            if (!lengthTrig) params[LENGTH_PARAM].setValue(0.f);
            if (!shiftTrig)  params[SHIFT_PARAM].setValue(0.f);
            if (!scaleTrig)  params[SCALE_PARAM].setValue(0.f);
            if (!saveTrig)   params[SAVE_PARAM].setValue(0.f);
            if (!recallTrig) params[RECALL_PARAM].setValue(0.f);
        }

        muteMode   = params[MUTE_PARAM].getValue()   > 0.5f;
        lengthMode = params[LENGTH_PARAM].getValue()  > 0.5f;
        shiftMode  = params[SHIFT_PARAM].getValue()   > 0.5f;
        scaleMode  = params[SCALE_PARAM].getValue()   > 0.5f;
        saveMode   = params[SAVE_PARAM].getValue()    > 0.5f;
        recallMode = params[RECALL_PARAM].getValue()  > 0.5f;

        bool lengthChanChanged = lengthMode && (editChan != prevSelectedChan);
        if ((!prevLengthMode && lengthMode) || lengthChanChanged)
            lengthSliderSnapshot[editChan] = params[SLIDER_PARAMS + editChan].getValue();
        if (prevLengthMode && !lengthMode)
            for (int ch = 0; ch < 8; ch++) lengthSliderSnapshot[ch] = -1.f;

        bool scaleChChanged = scaleMode && (editChan != prevSelectedChan);
        if ((!prevScaleMode && scaleMode) || scaleChChanged)
            scaleSliderSnapshot = params[SLIDER_PARAMS + editChan].getValue();
        if (prevScaleMode && !scaleMode) scaleSliderSnapshot = -1.f;
        prevSelectedChan = editChan;

        bool anyReleased = (prevMuteMode && !muteMode) || (prevLengthMode && !lengthMode) ||
                           (prevShiftMode && !shiftMode) || (prevScaleMode && !scaleMode) ||
                           (prevSaveMode && !saveMode) || (prevRecallMode && !recallMode);
        if (anyReleased) for (int i = 0; i < 16; i++) stepTrig[i].reset();

        prevMuteMode=muteMode; prevLengthMode=lengthMode; prevShiftMode=shiftMode;
        prevScaleMode=scaleMode; prevSaveMode=saveMode; prevRecallMode=recallMode;

        if (muteMode)   setRGB(MUTE_LIGHT,   0.6f,0.f,1.f); else clearRGB(MUTE_LIGHT);
        if (lengthMode) setRGB(LENGTH_LIGHT, 0.f,1.f,0.f);  else clearRGB(LENGTH_LIGHT);
        if (shiftMode)  setRGB(SHIFT_LIGHT,  1.f,1.f,1.f);  else clearRGB(SHIFT_LIGHT);
        if (scaleMode)  setRGB(SCALE_LIGHT,  0.f,0.4f,1.f); else clearRGB(SCALE_LIGHT);
        if (saveMode)   setRGB(SAVE_LIGHT,   1.f,0.6f,0.f); else clearRGB(SAVE_LIGHT);
        if (recallMode) setRGB(RECALL_LIGHT, 0.f,1.f,1.f);  else clearRGB(RECALL_LIGHT);

        bool noMode = !muteMode && !lengthMode && !shiftMode && !scaleMode && !saveMode && !recallMode;
        selectedChan = editChan;

        for (int i = 0; i < 16; i++) {
            if (!stepTrig[i].process(params[STEP_PARAMS + i].getValue())) continue;

            if (saveMode) {
                savePreset(i);
                params[SAVE_PARAM].setValue(0.f);
                saveAnim.trigger(i, 0.8f, false);
            }
            else if (recallMode) {
                recallPreset(i);
                params[RECALL_PARAM].setValue(0.f);
                saveAnim.trigger(i, 0.4f, true);
            }
            else if (muteMode) {
                if (i < 8) { editChan = i; chanMuted[i] = !chanMuted[i]; }
                else stepMuted[editChan][i] = !stepMuted[editChan][i];
            }
            else if (lengthMode) {
                if (i < 8) editChan = i;
            }
            else if (shiftMode) {
                if (i < 8) { editChan = i; }
                else {
                    switch (i) {
                        case 8:  for (int s = 0; s < 16; s++) stepCV[editChan][s] = 0.f; break;
                        case 9:  stepSmooth[editChan][seqPos[editChan]] = !stepSmooth[editChan][seqPos[editChan]]; break;
                        case 10: for (int s = 0; s < seqLength[editChan]; s++) stepCV[editChan][s] = random::uniform() * 4.f; break;
                        case 11: frozen[editChan] = !frozen[editChan]; break;
                        case 12: direction[editChan] = 0; break;
                        case 13: direction[editChan] = 1; break;
                        case 14: direction[editChan] = 2; break;
                        case 15: direction[editChan] = 3; break;
                        default: break;
                    }
                }
            }
            else if (scaleMode) {
                if (i < 8) editChan = i;
            }
            else {
                globalStep = (globalStep == i) ? -1 : i;
            }
        }

        // -----------------------------------------------------------
        // Live fader recording — ported verbatim, + momentary VU trigger
        // -----------------------------------------------------------
        if (noMode) {
            for (int ch = 0; ch < 8; ch++) {
                float sv = params[SLIDER_PARAMS + ch].getValue();
                if (std::abs(sv - prevSlider[ch]) > 0.0001f) {
                    int targetStep;
                    if (globalStep >= 0) targetStep = globalStep;
                    else if (stepHoldTimer[ch] > 0.f) targetStep = lastSeqPos[ch];
                    else targetStep = seqPos[ch];
                    stepCV[ch][targetStep] = sv;
                    prevSlider[ch] = sv;
                    chTweakTimer[ch] = 0.5f;
                }
            }
        } else {
            for (int ch = 0; ch < 8; ch++) prevSlider[ch] = params[SLIDER_PARAMS + ch].getValue();
        }
        for (int ch = 0; ch < 8; ch++) if (chTweakTimer[ch] > 0.f) chTweakTimer[ch] -= args.sampleTime;

        if (lengthMode) {
            float sv = params[SLIDER_PARAMS + editChan].getValue();
            float snap = lengthSliderSnapshot[editChan];
            if (snap < 0.f || std::abs(sv - snap) > LENGTH_DEADBAND) {
                int newLen = clamp((int)std::round(sv / 4.0f * 16.f), 1, 16);
                seqLength[editChan] = newLen;
                if (seqPos[editChan] >= seqLength[editChan]) seqPos[editChan] = 0;
            }
        }
        if (scaleMode) {
            float sv = params[SLIDER_PARAMS + editChan].getValue();
            float snap = scaleSliderSnapshot;
            if (snap < 0.f || std::abs(sv - snap) > SCALE_DEADBAND)
                scaleIndex[editChan] = clamp((int)(sv / 4.0f * 15.5f), 0, 15);
        }

        // -----------------------------------------------------------
        // Engine-mode dispatch (replaces OG's clkMode/divCount branch).
        // INT: master BPM + per-channel polyrhythm divisor (Knob 3).
        // EXT: clock "gearbox" (Knob 1) + sample-held offset (Knob 2,
        //      clock-aligned) + per-channel probability skip (Knob 3).
        // MOD: internal grid (Knob 1) + recentered CV bias (Knob 2) +
        //      S&H rate as a ratio of the grid (Knob 3).
        // -----------------------------------------------------------
        int engineMode = (int)params[ENGINE_MODE_PARAM].getValue();
        float k1 = params[KNOB1_PARAM].getValue();
        float k2 = params[KNOB2_PARAM].getValue();
        float k3 = params[KNOB3_PARAM].getValue();

        bool holding = inputs[RESET_INPUT].getVoltage() > 2.f;
        if (resetTrig.process(inputs[RESET_INPUT].getVoltage())) {
            for (int ch = 0; ch < 8; ch++) { seqPos[ch] = 0; subStepCount[ch] = 0; }
            internalPhase = 0.f; extGearPhase = 0.f; modSHPhase = 0.f;
        }

        timeSinceLastClock += args.sampleTime;
        bool realClockEdge = clockTrig.process(inputs[CLOCK_INPUT].getVoltage());
        if (realClockEdge) {
            if (timeSinceLastClock > 0.001f && timeSinceLastClock < 10.f)
                lastClockPeriod = timeSinceLastClock;
            timeSinceLastClock = 0.f;
        }

        if (!holding) {
            if (engineMode == ENGINE_INT) {
                float bpm = 20.f + k1 * 280.f;
                float stepPeriod = 60.f / (bpm * 4.f);
                internalPhase += args.sampleTime;
                if (internalPhase >= stepPeriod) {
                    internalPhase -= stepPeriod;
                    for (int ch = 0; ch < 8; ch++) {
                        if (frozen[ch]) continue;
                        int divisor = 1 + (int)std::round(k3 * ch);
                        if (++subStepCount[ch] >= divisor) {
                            subStepCount[ch] = 0;
                            lastSeqPos[ch] = seqPos[ch];
                            advanceChannel(ch);
                            stepHoldTimer[ch] = STEP_HOLD_WINDOW;
                        }
                    }
                }
            }
            else if (engineMode == ENGINE_EXT) {
                if (realClockEdge) { extOffsetHeld = (k2 - 0.5f) * 10.f; extGearPhase = 0.f; }
                float ratio = gearRatioFromKnob(k1);
                float gearPeriod = lastClockPeriod / std::max(ratio, 0.01f);
                extGearPhase += args.sampleTime;
                if (extGearPhase >= gearPeriod) {
                    extGearPhase -= gearPeriod;
                    for (int ch = 0; ch < 8; ch++) {
                        if (frozen[ch]) continue;
                        if (random::uniform() <= k3) {
                            lastSeqPos[ch] = seqPos[ch];
                            advanceChannel(ch);
                            stepHoldTimer[ch] = STEP_HOLD_WINDOW;
                        }
                    }
                }
            }
            else { // ENGINE_MOD
                float bpm = 20.f + k1 * 280.f;
                float gridPeriod = 60.f / (bpm * 4.f);
                float ratio = gearRatioFromKnob(k3);
                float shPeriod = gridPeriod / std::max(ratio, 0.01f);
                internalPhase += args.sampleTime;
                modSHPhase += args.sampleTime;
                if (internalPhase >= gridPeriod) internalPhase -= gridPeriod;
                if (modSHPhase >= shPeriod) {
                    modSHPhase -= shPeriod;
                    float biasedCV = clamp(inputs[CLOCK_INPUT].getVoltage() + (k2 - 0.5f) * 10.f, -5.f, 10.f);
                    for (int ch = 0; ch < 8; ch++) {
                        if (frozen[ch]) continue;
                        stepCV[ch][seqPos[ch]] = biasedCV;
                        lastSeqPos[ch] = seqPos[ch];
                        advanceChannel(ch);
                        stepHoldTimer[ch] = STEP_HOLD_WINDOW;
                    }
                }
            }
        }
        for (int ch = 0; ch < 8; ch++) if (stepHoldTimer[ch] > 0.f) stepHoldTimer[ch] -= args.sampleTime;

        // -----------------------------------------------------------
        // Output stage — ported verbatim, + mode-dependent offset + Gate override
        // -----------------------------------------------------------
        for (int ch = 0; ch < 8; ch++) {
            int pos = seqPos[ch];
            bool muted = chanMuted[ch] || stepMuted[ch][pos];
            bool frz   = frozen[ch];
            float bright = (ch == editChan) ? 1.0f : 0.3f;
            if (muted)      setRGB(CHANNEL_LIGHTS + ch*3, 0.6f*bright, 0.f, 1.f*bright);
            else if (frz)   setRGB(CHANNEL_LIGHTS + ch*3, bright, bright, bright);
            else            setRGB(CHANNEL_LIGHTS + ch*3, bright, 0.f, 0.f);

            if (muted) { outputs[CV_OUTPUTS + ch].setVoltage(0.f); continue; }

            float v = stepCV[ch][pos];
            if (scaleIndex[ch] > 0 && scaleIndex[ch] < 15)
                v = quantizeVoltage(v / 4.0f, scaleIndex[ch]) * 4.0f;
            if (stepSmooth[ch][pos]) {
                float glideTime = std::max(lastClockPeriod * 0.9f, 0.25f);
                float rate = 1.0f / (args.sampleRate * glideTime);
                glideCV[ch] += (v - glideCV[ch]) * rate;
                v = glideCV[ch];
            } else {
                glideCV[ch] = v;
            }

            if (engineMode == ENGINE_INT)      v += (k2 - 0.5f) * 10.f;
            else if (engineMode == ENGINE_EXT) v += extOffsetHeld;
            // ENGINE_MOD: bias already baked into the sampled CV before storage

            int chm = (int)params[CHMODE_PARAMS + ch].getValue();
            float outVal = (chm == CH_GATE) ? ((v > 2.0f) ? 10.f : 0.f) : v;
            outVal = clamp(outVal, -5.f, 10.f);
            outputs[CV_OUTPUTS + ch].setVoltage(outVal);
        }

        // -----------------------------------------------------------
        // Save/recall animation + step-button feedback — ported verbatim
        // -----------------------------------------------------------
        if (saveAnim.active) {
            saveAnim.timer += args.sampleTime;
            if (saveAnim.timer >= saveAnim.duration) saveAnim.active = false;
        }
        float animProgress = saveAnim.active ? clamp(saveAnim.timer / saveAnim.duration, 0.f, 1.f) : -1.f;
        int animFill = saveAnim.active ? (int)std::floor(animProgress * (saveAnim.slot + 1)) : -1;

        for (int i = 0; i < 16; i++) {
            bool isCurrent = (i == seqPos[editChan]);
            bool isMuted   = stepMuted[editChan][i];
            bool inLen     = (i < seqLength[editChan]);

            if (saveAnim.active && !saveAnim.isRecall) {
                if (i <= saveAnim.slot && i <= animFill) setRGB(BUTTON_LIGHTS + i*3, 1.f, 0.6f, 0.f);
                else clearRGB(BUTTON_LIGHTS + i*3);
            }
            else if (saveMode) {
                float b = presetValid[i] ? 0.7f : 0.15f;
                setRGB(BUTTON_LIGHTS + i*3, b, b*0.6f, 0.f);
            }
            else if (saveAnim.active && saveAnim.isRecall) {
                if (i == saveAnim.slot) setRGB(BUTTON_LIGHTS + i*3, 0.f, 1.f, 1.f);
                else if (presetValid[i]) setRGB(BUTTON_LIGHTS + i*3, 0.f, 0.4f, 0.4f);
                else clearRGB(BUTTON_LIGHTS + i*3);
            }
            else if (recallMode) {
                float b = presetValid[i] ? 0.8f : 0.05f;
                setRGB(BUTTON_LIGHTS + i*3, 0.f, b, b);
            }
            else if (muteMode) {
                float b = (i < 8) ? (chanMuted[i] ? 1.f : 0.f) : (stepMuted[editChan][i] ? 1.f : 0.f);
                setRGB(BUTTON_LIGHTS + i*3, 0.6f*b, 0.f, 1.f*b);
            }
            else if (lengthMode) {
                float b = (i == seqLength[editChan]-1) ? 1.f : (inLen ? 0.4f : 0.f);
                setRGB(BUTTON_LIGHTS + i*3, 0.f, b, 0.f);
            }
            else if (scaleMode) {
                float b = (i == scaleIndex[editChan]) ? 1.f : 0.f;
                setRGB(BUTTON_LIGHTS + i*3, 0.f, 0.4f*b, 1.f*b);
            }
            else if (shiftMode) {
                float b = (i >= 8) ? 0.3f : 0.f;
                setRGB(BUTTON_LIGHTS + i*3, b, b, b);
            }
            else {
                if (isCurrent) setRGB(BUTTON_LIGHTS + i*3, 1.f, 0.f, 0.f);
                else if (globalStep == i) setRGB(BUTTON_LIGHTS + i*3, 0.5f, 0.f, 0.f);
                else if (isMuted && inLen) setRGB(BUTTON_LIGHTS + i*3, 0.1f, 0.f, 0.15f);
                else if (inLen) setRGB(BUTTON_LIGHTS + i*3, 0.05f, 0.f, 0.f);
                else clearRGB(BUTTON_LIGHTS + i*3);
            }
        }

        for (int ch = 0; ch < 8; ch++) {
            int pos = seqPos[ch];
            bool muted = chanMuted[ch] || stepMuted[ch][pos];
            bool frz   = frozen[ch];
            float bright = (ch == editChan) ? 1.0f : 0.3f;
            if (muted)    setRGB(CHANNEL_LIGHTS + ch*3, 0.6f*bright, 0.f, 1.f*bright);
            else if (frz) setRGB(CHANNEL_LIGHTS + ch*3, bright, bright, bright);
            else          setRGB(CHANNEL_LIGHTS + ch*3, bright, 0.f, 0.f);
        }

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
        arrF("stepCV", [&](json_t* a){ for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) json_array_append_new(a, json_real(stepCV[ch][s])); });
        arrF("seqLength", [&](json_t* a){ for(int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(seqLength[ch])); });
        arrF("direction",  [&](json_t* a){ for(int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(direction[ch])); });
        arrF("pendDir",    [&](json_t* a){ for(int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(pendDir[ch])); });
        arrF("scaleIndex", [&](json_t* a){ for(int ch=0;ch<8;ch++) json_array_append_new(a, json_integer(scaleIndex[ch])); });
        arrF("chanMuted",  [&](json_t* a){ for(int ch=0;ch<8;ch++) json_array_append_new(a, json_boolean(chanMuted[ch])); });
        arrF("stepMuted",  [&](json_t* a){ for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) json_array_append_new(a, json_boolean(stepMuted[ch][s])); });
        arrF("stepSmooth", [&](json_t* a){ for(int ch=0;ch<8;ch++) for(int s=0;s<16;s++) json_array_append_new(a, json_boolean(stepSmooth[ch][s])); });
        arrF("frozen",     [&](json_t* a){ for(int ch=0;ch<8;ch++) json_array_append_new(a, json_boolean(frozen[ch])); });
        arrF("presetKnob3",[&](json_t* a){ for(int s=0;s<16;s++) json_array_append_new(a, json_real(presetKnob3[s])); });
        arrF("presetValid",[&](json_t* a){ for(int s=0;s<16;s++) json_array_append_new(a, json_boolean(presetValid[s])); });
        json_object_set_new(root, "selectedChan", json_integer(selectedChan));
        json_object_set_new(root, "engineMode", json_real(params[ENGINE_MODE_PARAM].getValue()));
        json_object_set_new(root, "knob1", json_real(params[KNOB1_PARAM].getValue()));
        json_object_set_new(root, "knob2", json_real(params[KNOB2_PARAM].getValue()));
        json_object_set_new(root, "knob3", json_real(params[KNOB3_PARAM].getValue()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        auto getI = [&](const char* k, int idx){ json_t* a=json_object_get(root,k); if(!a) return 0; json_t* v=json_array_get(a,idx); return v?(int)json_integer_value(v):0; };
        auto getF = [&](const char* k, int idx){ json_t* a=json_object_get(root,k); if(!a) return 0.f; json_t* v=json_array_get(a,idx); return v?(float)json_real_value(v):0.f; };
        auto getB = [&](const char* k, int idx){ json_t* a=json_object_get(root,k); if(!a) return false; json_t* v=json_array_get(a,idx); return v?json_boolean_value(v):false; };

        int idx = 0;
        for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++) stepCV[ch][s] = getF("stepCV", idx++);
        for (int ch=0;ch<8;ch++) seqLength[ch]  = getI("seqLength", ch);
        for (int ch=0;ch<8;ch++) direction[ch]  = getI("direction", ch);
        for (int ch=0;ch<8;ch++) pendDir[ch]    = getI("pendDir", ch);
        for (int ch=0;ch<8;ch++) scaleIndex[ch] = getI("scaleIndex", ch);
        for (int ch=0;ch<8;ch++) chanMuted[ch]  = getB("chanMuted", ch);
        for (int ch=0;ch<8;ch++) frozen[ch]     = getB("frozen", ch);
        for (int s=0;s<16;s++)  presetKnob3[s]  = getF("presetKnob3", s);
        for (int s=0;s<16;s++)  presetValid[s]  = getB("presetValid", s);
        idx = 0; for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++) stepMuted[ch][s]  = getB("stepMuted", idx++);
        idx = 0; for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++) stepSmooth[ch][s] = getB("stepSmooth", idx++);

        json_t* sc = json_object_get(root, "selectedChan"); if (sc) selectedChan = (int)json_integer_value(sc);
        json_t* em = json_object_get(root, "engineMode"); if (em) params[ENGINE_MODE_PARAM].setValue((float)json_real_value(em));
        json_t* k1 = json_object_get(root, "knob1"); if (k1) params[KNOB1_PARAM].setValue((float)json_real_value(k1));
        json_t* k2 = json_object_get(root, "knob2"); if (k2) params[KNOB2_PARAM].setValue((float)json_real_value(k2));
        json_t* k3 = json_object_get(root, "knob3"); if (k3) params[KNOB3_PARAM].setValue((float)json_real_value(k3));
    }

    void onRandomize(const RandomizeEvent& e) override {
        for (int ch=0;ch<8;ch++) for (int s=0;s<16;s++) stepCV[ch][s] = random::uniform() * 4.f;
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for (int ch = 0; ch < 8; ch++) {
            for (int s = 0; s < 16; s++) { stepCV[ch][s]=0.f; stepMuted[ch][s]=false; stepSmooth[ch][s]=false; }
            seqLength[ch]=16; seqPos[ch]=0; direction[ch]=0; pendDir[ch]=1;
            scaleIndex[ch]=0; chanMuted[ch]=false; frozen[ch]=false; glideCV[ch]=0.f;
            subStepCount[ch]=0; stepHoldTimer[ch]=0.f; chTweakTimer[ch]=0.f;
        }
        selectedChan=0; prevSelectedChan=0;
        editChan=0; globalStep=-1; glowPhase=0.f;
        scaleSliderSnapshot=-1.f;
        for (int ch=0;ch<8;ch++) lengthSliderSnapshot[ch]=-1.f;
        internalPhase=0.f; extGearPhase=0.f; extOffsetHeld=0.f; modSHPhase=0.f;
    }
};

// ============================================================
// SoundscapeSlider — ported verbatim from Skyline's SlimFader (same
// dual-branch desktop/firmware geometry fix), renamed and pointed at
// the SoundscapeFaderBg/Handle SVGs already in res/.
// ============================================================
struct SoundscapeSlider : app::SvgSlider {
    // NOTE: OG Skyline's SlimFader used a desktop/firmware TH=60/40 split,
    // but that only works if there's a correspondingly-sized 60px asset for
    // desktop. SoundscapeFaderBg.svg is a single 6x40 asset, so a TH=60
    // override here just draws the native 40px rect inside a taller box,
    // leaving the bottom ~20px empty (showing the panel color through —
    // the two-tone look). Sizing to the actual asset fixes it outright.
    static const int TW=6, TH=40, HW=14, HH=8, TM=6;
    bool  dragging     = false;
    float dragStartVal = 0.f;

    SoundscapeSlider() {
        setBackgroundSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SoundscapeFaderBg.svg")));
        setHandleSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SoundscapeFaderHandle.svg")));

        background->box.size = Vec(TW, TH);
        background->box.pos  = Vec((HW - TW) / 2.f, 0.f);
        handle->box.size     = Vec(HW, HH);
        setHandlePosCentered(Vec(HW/2.f, TH - HH/2.f), Vec(HW/2.f, TM + HH/2.f));
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

// ============================================================
// EditRingLight — ported verbatim from Skyline.cpp.
// ============================================================
struct EditRingLight : widget::Widget {
    int     lightId = 0;
    Module* mod     = nullptr;

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
// SoundscapePort — ported verbatim from Skyline's SkylinePort.
// ============================================================
struct SoundscapePort : app::SvgPort {
    SoundscapePort() {
        setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/SoundscapeJack.svg")));
    }
};

// ============================================================
// ChModeButton — NEW. A single combo widget that is both clickable
// (cycles CV -> Pitch -> Gate on its own param) and self-drawing
// (shows the C/P/G glyph, or a momentary green->red VU bar while
// that channel's slider is being moved). Replaces the earlier
// two-widget approach (separate display + separate VCVButton) to
// save vertical space, per request.
// ============================================================
struct ChModeButton : ParamWidget {
    int channelId = 0;

    ChModeButton() { box.size = Vec(24, 20); }

    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            if (getParamQuantity()) {
                int cur = (int)getParamQuantity()->getValue();
                getParamQuantity()->setValue((cur + 1) % 3);
            }
            e.consume(this);
        }
        ParamWidget::onButton(e);
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
        nvgFillColor(args.vg, nvgRGBA(0x1a, 0x1a, 0x1a, 0xff));
        nvgFill(args.vg);

        // `module` here is ParamWidget's own engine::Module* — cast to the
        // concrete type to read Soundscape-specific state for drawing.
        auto* mod = dynamic_cast<Soundscape*>(module);
        if (!mod) return;

        bool tweaking = mod->chTweakTimer[channelId] > 0.f;
        if (tweaking) {
            float strength = mod->params[Soundscape::SLIDER_PARAMS + channelId].getValue() / 4.f;
            int bars = clamp((int)std::round(strength * 4.f) + 1, 1, 5);
            for (int b = 0; b < bars; b++) {
                float t = (float)b / 4.f; // green (0) -> red (1)
                nvgBeginPath(args.vg);
                nvgRect(args.vg, 3, box.size.y - 4 - (b * 3.6f), box.size.x - 6, 2.6f);
                nvgFillColor(args.vg, nvgRGBA((int)(t * 255), (int)((1.f - t) * 200), 0, 255));
                nvgFill(args.vg);
            }
        } else {
            int chm = getParamQuantity() ? (int)getParamQuantity()->getValue() : 0;
            const char* glyph = "C";
            if (chm == Soundscape::CH_PITCH) glyph = "P";
            else if (chm == Soundscape::CH_GATE) glyph = "G";
            nvgFontSize(args.vg, 16.f);
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x9d, 0x00, 0xff));
            nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f, glyph, nullptr);
        }
    }
};

// ============================================================
// Widget — coordinates mirror Skyline's exact layout. The new
// per-channel ChModeButton is inserted directly above the output
// jack, using up part of the existing whitespace between the old
// output/LED pair and the controls row below, rather than growing
// the panel.
// ============================================================
struct SoundscapeWidget : ModuleWidget {
    SoundscapeWidget(Soundscape* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Soundscape.svg")));

        const float cX[8] = {7.00f,19.51f,32.03f,44.54f,57.06f,69.57f,82.09f,94.60f};
        const float xJack = 7.00f, xSwitch = 20.00f;
        const float xK1 = 32.0f, xK2 = 48.5f, xK3 = 65.0f;
        const float xB1 = 78.5f, xB2 = 87.0f, xB3 = 95.5f;

        // NEW row, inserted above the OG output/LED pair.
        const float yChMode = 17.0f;
        // Shifted down from OG's 22.5/31.0 to make room for yChMode above,
        // eating into what was empty whitespace before the controls row.
        const float yOut = 29.0f, yLed = 37.0f;

        const float yClk = 46.0f, yKnob = 53.5f, yRst = 61.0f;
        const float yB1 = 46.0f, yB2 = 61.0f;
        const float ySld = 70.0f, yS1 = 104.0f, yS2 = 119.0f;

        for (int ch = 0; ch < 8; ch++) {
            auto* cm = createParam<ChModeButton>(
                mm2px(Vec(cX[ch], yChMode)).minus(Vec(12, 10)),
                module, Soundscape::CHMODE_PARAMS + ch);
            cm->channelId = ch;
            addParam(cm);

            addOutput(createOutputCentered<SoundscapePort>(mm2px(Vec(cX[ch], yOut)), module, Soundscape::CV_OUTPUTS + ch));

            auto* ring = new EditRingLight;
            ring->mod = module;
            ring->lightId = Soundscape::EDIT_RING_LIGHTS + ch;
            ring->box.pos = mm2px(Vec(cX[ch], yLed)).minus(ring->box.size.div(2.f));
            addChild(ring);

            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(cX[ch], yLed)), module, Soundscape::CHANNEL_LIGHTS + ch * 3));
        }

        addInput(createInputCentered<SoundscapePort>(mm2px(Vec(xJack, yClk)), module, Soundscape::CLOCK_INPUT));
        addInput(createInputCentered<SoundscapePort>(mm2px(Vec(xJack, yRst)), module, Soundscape::RESET_INPUT));
        addParam(createParamCentered<CKSSThree>(mm2px(Vec(xSwitch, yKnob)), module, Soundscape::ENGINE_MODE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK1, yKnob)), module, Soundscape::KNOB1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK2, yKnob)), module, Soundscape::KNOB2_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(xK3, yKnob)), module, Soundscape::KNOB3_PARAM));

        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB1, yB1)), module, Soundscape::MUTE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB1, yB1)), module, Soundscape::MUTE_LIGHT));

        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB2, yB1)), module, Soundscape::LENGTH_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB2, yB1)), module, Soundscape::LENGTH_LIGHT));

        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB3, yB1)), module, Soundscape::SHIFT_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB3, yB1)), module, Soundscape::SHIFT_LIGHT));

        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB1, yB2)), module, Soundscape::SCALE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB1, yB2)), module, Soundscape::SCALE_LIGHT));

        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB2, yB2)), module, Soundscape::SAVE_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB2, yB2)), module, Soundscape::SAVE_LIGHT));

        addParam(createParamCentered<VCVLatch>(mm2px(Vec(xB3, yB2)), module, Soundscape::RECALL_PARAM));
        addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(xB3, yB2)), module, Soundscape::RECALL_LIGHT));

        for (int ch = 0; ch < 8; ch++)
            addParam(createParam<SoundscapeSlider>(mm2px(Vec(cX[ch] - 2.37f, ySld)), module, Soundscape::SLIDER_PARAMS + ch));

        for (int i = 0; i < 8; i++) {
            addParam(createParamCentered<VCVButton>(mm2px(Vec(cX[i], yS1)), module, Soundscape::STEP_PARAMS + i));
            addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(cX[i], yS1)), module, Soundscape::BUTTON_LIGHTS + i * 3));
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(cX[i], yS1 - 5.f)), module, Soundscape::STEP_LIGHTS + i * 3));

            addParam(createParamCentered<VCVButton>(mm2px(Vec(cX[i], yS2)), module, Soundscape::STEP_PARAMS + 8 + i));
            addChild(createLightCentered<MediumSimpleLight<RedGreenBlueLight>>(mm2px(Vec(cX[i], yS2)), module, Soundscape::BUTTON_LIGHTS + (8 + i) * 3));
            addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(mm2px(Vec(cX[i], yS2 - 5.f)), module, Soundscape::STEP_LIGHTS + (8 + i) * 3));
        }

        // All panel text (title, channel numbers, knob/switch labels +
        // the new double-duty keyword sub-labels, MUTE/LEN/SHIFT,
        // SCALE/SAVE/RECALL, the SHIFT-function labels, and the
        // steps-9-16 numbering) is baked into the panel SVG as outlined
        // paths — no runtime text, matching Skyline's documented fix for
        // why fonts can't be relied on at runtime on the MetaModule.
    }
};

Model* modelSoundscape = createModel<Soundscape, SoundscapeWidget>("SoundscapeMM");
