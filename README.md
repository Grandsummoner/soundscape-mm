# Skyline-MM: 8-Channel CV Sequencer for 4ms MetaModule

**Skyline-MM** is a high-performance, 8-channel CV sequencer compiled specifically for the **4ms MetaModule** Eurorack hardware. Adapted from the desktop VCV Rack module, this version is optimized for immediate, hardware-native control, mapping Skyline's dense user interface onto the MetaModule's tactile knobs, screen navigation, and physical analog patch points.

---

## 1. What to Expect (Default Hardware Layout)

When you load Skyline-MM on your hardware for the first time, the MetaModule uses an **Auto-Map** routing to bind physical controls directly to Skyline's parameters. This provides a plug-and-play performance layout right out of the box.

### Physical Knobs & Trimmers (The Default Performance Page)
The MetaModule's 12 knobs map directly to Skyline's virtual controls as follows:

| Hardware Control | Virtual Parameter in Skyline | Performance Description |
| :---: | :--- | :--- |
| **Knob A** | `SLIDER_PARAMS` Ch 1 | Edits step CV for **Channel 1** |
| **Knob B** | `SLIDER_PARAMS` Ch 2 | Edits step CV for **Channel 2** |
| **Knob C** | `SLIDER_PARAMS` Ch 3 | Edits step CV for **Channel 3** |
| **Knob D** | `SLIDER_PARAMS` Ch 4 | Edits step CV for **Channel 4** |
| **Knob E** | `SLIDER_PARAMS` Ch 5 | Edits step CV for **Channel 5** |
| **Knob F** | `SLIDER_PARAMS` Ch 6 | Edits step CV for **Channel 6** |
| **Trimmer y** | `SLIDER_PARAMS` Ch 7 | Edits step CV for **Channel 7** |
| **Trimmer z** | `SLIDER_PARAMS` Ch 8 | Edits step CV for **Channel 8** |
| **Trimmer u** | `DIVIDE_PARAM` | Adjusts master Clock Division (1 to 16) |
| **Trimmer v** | `OFFSET_PARAM` | Adjusts master output Offset (-5V to +5V) |
| **Trimmer w** | `ATTENUATE_PARAM` | Adjusts master Attenuation level (0% to 100%) |
| **Trimmer x** | *Unassigned* | Left free for custom user mapping |

---

### Physical Patch Sockets (Hardware I/O)
The jacks on the bottom of the MetaModule connect 1:1 to Skyline's essential virtual ports:

*   **Gate In 1:** Master **CLOCK Input** (advances active sequence channels).
*   **Gate In 2:** Master **RESET Input** (returns playheads to step 1).
*   **Audio/CV Outs 1–8:** Dedicated **Outputs** for Skyline's 8 independent CV channels. Connect these directly to other hardware modules in your Eurorack case.
*   **Audio/CV Ins 1–6:** Available as analog CV sources for custom modulation mapping.

---

## 2. How to Perform Standalone

### Adjusting Sequences & Global Settings
*   To edit step values, simply turn **Knobs A–F** (Channels 1–6) or **Trimmers y–z** (Channels 7–8).
*   If **Global Step-Lock** is unlocked, turning a channel's knob will continuously live-record values to the active playhead step.
*   To adjust master tempo scaling, use the **u, v, and w trimmers** on the left side of the panel to alter clock division, offset, and attenuation.

### Navigating Latched Modes (Mute, Length, Scale, etc.)
Because the MetaModule lacks physical buttons for every virtual switch, menu navigation is handled via the hardware encoder:
1.  **Select a Control:** Rotate the physical **Navigate Encoder** (top right) to move the virtual cursor over the six latch buttons (`MUTE`, `LEN`, `SHIFT`, `SCALE`, `SAVE`, `RECALL`) on-screen.
2.  **Toggle a Mode:** Click the encoder down to "press" the highlighted latch button. The screen will color-code to indicate the active mode (e.g., Purple for Mute mode, Green for Length mode).
3.  **Exit/Go Back:** Click the physical cream-colored **Back Button** (below the encoder) to exit the current mode and return to the main performance dashboard.

---

## 3. How to Remap & Customize

If you want to change the default layout (for example, mapping external CV to automate a sequence, or putting all 8 sliders on a single custom page), the MetaModule offers two methods:

### Method A: On-Hardware Remapping (No PC Required)
1.  Double-click the physical **Navigate Encoder** to open the MetaModule’s system menu.
2.  Select **Mapping** and use the cursor to select any parameter on the Skyline display interface.
3.  Turn any physical trimmer or large knob to assign it.
4.  To route external modulation, select a virtual slider on the screen, go to the mapping menu, and assign it to one of the physical **Audio/CV Inputs (1–6)**. You can now patch an external LFO or Envelope to automate step values or sequence behavior.

### Method B: Using the MetaModule Hub (In VCV Rack Desktop)
For complex multi-module patches, it is easiest to design your custom controls on your computer first:
1.  Open VCV Rack on your desktop and load the **MetaModule Hub** module.
2.  Place Skyline alongside the Hub.
3.  Drag virtual cables or mapping routes from the Hub’s visual representations of the MetaModule knobs/jacks directly to Skyline's inputs and parameters.
4.  Save the patch (`.yml` format) directly to your MicroSD card. When loaded on the physical MetaModule, your routing configuration will load exactly as designed.

---

## 4. Compilation and Installation

To build Skyline-MM and load it onto your hardware:

### Prerequisites
Make sure you have the following installed on your host computer:
*   [CMake](https://cmake.org/) (v3.22 or later)
*   The [Arm GNU Toolchain](https://developer.arm.com/Tools%20and%20Software/GNU%20Toolchain) (`arm-none-eabi-gcc` compiler)
*   The [4ms MetaModule Plugin SDK](https://github.com/4ms/metamodule-plugin-sdk) cloned locally or added as a submodule.

### Build Command
Compile the plugin targeting the MetaModule architecture using CMake:
```bash
cmake -B build -D METAMODULE_SDK_DIR=/path/to/metamodule-plugin-sdk
cmake --build build
