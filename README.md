![Module Graphic](./assets/Skyline.png)

# Skyline

Skyline is an 8-channel CV sequencer designed for **VCV Rack v2** and optimized to compile for the **4ms Metamodule**. Heavily inspired by Eurorack sequencers like the Malekko Voltage Block, it provides flexible, high-density modulation, built-in quantization, and preset management in a single 20HP panel.

The codebase is structured to compile natively on both desktop platforms and the Metamodule hardware with 100% layout and functionality parity.

---

## Features

* **8 Independent Channels:** Output up to 8 unique CV sequences concurrently, with dedicated physical outputs and visual edit targets.
* **16 Steps per Channel:** Program CV values, mute states, and glide settings independently for each step.
* **16 Quantizer Scales:** Quantize CV values to Major, Minor, Pentatonic, Locrian, Blues, and more.
* **Mode-Based Editing:** Quickly adjust muting, loop lengths, scale quantizing, and global shifts using the tactile button interface.
* **Step Utilities:** Set clear, glide/smooth, randomization, loop freeze, and playback directions (Forward, Reverse, Pendulum, Random) per channel.
* **16 Save/Recall Slots:** Save complete sequencer states (CV, mutes, lengths, clock settings) and instantly recall them on the fly.
* **Dual-Target Compilation:** Preprocessor-separated widget dimensions allow a full-range 60px vertical fader travel on desktop VCV Rack, while preserving the original 40px visual constraints and memory requirements when compiled for the 4ms Metamodule hardware.

---

## Detailed Quantizer Scales

Skyline contains a built-in 1V/Oct quantizer. Sliders operate natively on a `0.0V` to `4.0V` range (representing 4 complete octaves). When a quantizer scale is active, the output voltage is mathematically scaled to semitones (`voltage * 12`), evaluated for its octaval offset, snapped to the nearest valid interval in the active scale array, and then reconstructed back to output voltage.

The 16 selectable scales correspond to **Step Buttons 1–16** when **SCALE** mode is active:

| Button | Scale Name | Semitone Intervals (Relative to Root) | Musical Characteristics & Style |
| :---: | :--- | :--- | :--- |
| **1** | **Unquantized** | Continuous | Smooth, continuous voltage (no quantization). |
| **2** | **Japanese (In)** | `0, 1, 5, 7, 10` | Traditional *Miyako-bushi* pentatonic scale. Dark and evocative. |
| **3** | **Major Pentatonic** | `0, 2, 4, 7, 9` | Classic bright, folk-friendly 5-note scale. Harmonically safe. |
| **4** | **Minor Pentatonic** | `0, 3, 5, 7, 10` | Standard blues/rock soloing scale. |
| **5** | **Blues** | `0, 3, 5, 6, 7, 10` | Minor pentatonic scale with an added diminished 5th ("blue note"). |
| **6** | **Locrian** | `0, 1, 3, 4, 6, 8, 10` | Diminished, tense, and highly dissonant modal scale. |
| **7** | **Arabian** | `0, 2, 4, 5, 6, 8, 10` | Chromatic intervals. Exotic, Middle-Eastern modal flavor. |
| **8** | **Phrygian** | `0, 1, 3, 5, 7, 8, 10` | Minor scale with a flat 2nd. Spanish/flamenco sounding. |
| **9** | **Natural Minor** | `0, 2, 3, 5, 7, 8, 10` | Aeolian mode. Classic sad, melancholic scale. |
| **10** | **Dorian** | `0, 2, 3, 5, 7, 9, 10` | Minor scale with a major 6th. Bright minor, highly popular in jazz. |
| **11** | **Mixolydian** | `0, 2, 4, 5, 7, 9, 10` | Major scale with a flat 7th. Classic rock, dominant blues scale. |
| **12** | **Persian** | `0, 1, 4, 5, 7, 8, 11` | Major scale with lowered 2nd and 6th, featuring distinctive augmented seconds. |
| **13** | **Double Harmonic** | `0, 1, 4, 5, 7, 8, 11` | Byzantine scale. Major scale with flat 2nd and 6th, sounding exotic and tense. |
| **14** | **Major** | `0, 2, 4, 5, 7, 9, 11` | Standard Ionian mode. Bright, diatonic, and familiar. |
| **15** | **Lydian** | `0, 2, 4, 6, 7, 9, 11` | Major scale with an augmented 4th. Dreamy, spacey, and cinematic. |
| **16** | **Chromatic** | `0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11` | Equal-tempered 12-note scale. Snaps directly to semitones. |

---

## Technical Specifications

* **Panel Width:** 20HP (300 px)
* **Panel Height:** 128.5mm (380 px)
* **Outputs:** 8 CV Outputs (`-5V` to `+10V` range, depending on Attenuate and Offset settings).
* **Inputs:** 1 Clock Input, 1 Reset / Hold Input.
* **Parameter Ranges:** Sliders output a base value of `0.0V` to `4.0V`.

---

## Interface & Controls

### Global Controls
* **Clock Mode Switch:** 
  * `CLK`: Advances the active sequence on clock triggers. 
  * `CV`: Directly addresses active steps dynamically using incoming CV (0–10V mapped to active sequence length).
  * `SLAVE`: Synchronizes sequence tracking.
* **Offset:** Adds global offset (`-5.0V` to `+5.0V`) to the selected channel outputs.
* **Attenuate:** Multiplies selected channel output CV between `0.0` and `1.0`.
* **Divide:** Configures clock division (divides incoming clock by 1 to 16).

### Functional Modes (Latching Buttons)
Activating a mode lets you edit parameters using the bottom 16 step buttons:

* **MUTE:** Buttons 1–8 mute the corresponding output channels. Buttons 9–16 mute individual steps for the selected channel.
* **LENGTH:** Sets loop lengths. Tap step buttons 1–16 to set the step length (e.g., tap step 8 to set an 8-step loop).
* **SCALE:** Selects quantization scales for the active channel (Major, Minor, Pentatonic, Locrian, etc.) matching step buttons 1–16.
* **SHIFT:** Activating Shift exposes secondary step functions:
  * **Step 8 (CLEAR):** Clears all steps on the current channel.
  * **Step 9 (SMOOTH):** Toggles portamento/glide on the current active step.
  * **Step 10 (RND):** Randomizes the step CV on the active channel.
  * **Step 11 (FREEZE):** Pauses/Freezes sequence advancement on the active channel.
  * **Step 12–15 (PLAYBACK DIRECTIONS):** Sets playback direction:
    * Step 12: Forward (`FWD`)
    * Step 13: Reverse (`REV`)
    * Step 14: Pendulum (`PEND`)
    * Step 15: Random Sequence (`RNDSEQ`)
* **SAVE / RECALL:** Saves or recalls the entire sequencer state into one of the 16 preset slots (assigned to step buttons 1–16).

---
