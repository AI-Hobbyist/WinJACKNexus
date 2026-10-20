# WinJACKNexus

[简体中文](README.md) · [日本語](README.ja.md) · [English](README.en.md) · [한국어](README.ko.md)

A modular audio and MIDI routing application for Windows x64. Connect JACK ports in a visual patchbay and combine devices, mixers, native effects, VST hosts and subgraphs into your own processing chain.

## Available features

| Feature | Contents |
| --- | --- |
| Patchbay | Visual audio/MIDI connections, project save/restore and subgraphs |
| Device nodes | WASAPI input/output and system MIDI input/output |
| Mixers | Digital mixer and 8/16/24-channel Classic Mixer |
| Native Fx | Gate, 31-band Graphic EQ, De-Esser, Compressor, Limiter, Delay, Parametric EQ and Reverb |
| Meterbridge | Multichannel meters, history charts and CSV recording |
| VST hosting | VST3; local builds also support VST2 with a separately supplied SDK |
| MIDI Tools | Channel splitter, 16×16 matrix and SF2/SFZ instrument loading |
| Control Deck | Signal switches, sound buttons, mono peak meters and multichannel gain knobs |
| Appearance and language | Theme tokens and custom themes; Chinese, Japanese, English and Korean |

## Control Deck

Every version is a separate DLL and supports both main graphs and subgraphs. Nodes have no individual GUI; open Control Deck from the quick-access bar.

| Control | Versions and behavior |
| --- | --- |
| Signal switch | Mono, stereo, 5.1, 7.1 and MIDI; new switches are enabled and pass signals by default |
| Sound button | Stereo output only; a click or global shortcut plays once, and retriggering restarts playback |
| Peak meter | Mono input only, no output port; −72 to +12 dBFS; right-click to select horizontal or vertical orientation |
| Gain knob | Mono, stereo, 5.1 and 7.1; one knob controls all channels of its node, with an adjustable dB range |

- Drag controls in the editor. Ctrl-click or drag a selection, then use the context menu for size, color, grouping and free/grid alignment. Groups can be named and dragged together. Signal switches may be mutually exclusive or independent.
- Names follow graph node names. Colors inherit the theme unless overridden per control.
- Locking hides the editor background and keeps the arranged controls at their existing positions above ordinary application windows. The quick-access dropdown provides lock, unlock, mouse click-through and Open editor.
- Control opacity affects controls only. With click-through enabled, use Alt+left-click to operate a control; for a gain knob this opens numeric entry.
- Right-click a sound button to load a file or assign a global shortcut. It works while another application is foreground; conflicts preserve the previous assignment. Supported formats depend on BASS/BASSmix and installed compatible decoders. WAV, AIFF, MP3, OGG and MOD have been checked. Put extra x64 BASS decoder DLLs in `signal_switch/bass_plugins/` and restart.
- Projects store Control Deck layout, window size/position, groups, relations, colors, dimensions, control opacity, click-through and node settings as Base64. Sound paths and shortcuts are saved, while sound files remain external. Loading a project never starts playback automatically.

All control nodes use the `Control Deck` category and `nodes/control_deck/` directory. Place `control_deck.dll` beside the EXE; panel translations remain in `signal_switch/locate/`. Old project paths under `nodes/signal_switch/` resolve to the installed new directory, retaining node DLL names and saved identities. Feed the peak meter from a signal branch rather than inserting it in series.

## Download and run

Windows x64, JACK2 and Microsoft Visual C++ runtimes are required. Extract with the directory structure intact and run `WinJACKNexus.exe`. Check JACK configuration and routing before connecting real devices.

Use **Actions → Build full Release package → Run workflow** on GitHub for a full Release build. After success, download the `WinJACKNexus-Release-x64-VST3` artifact. It contains the full application ZIP, SHA256 checksum, Qt runtime, all current modules, and JACK2 plus VC++ x64/x86 installers under `prerequisites/`. Install these prerequisites first. This Action disables VST2 and keeps VST3. The first cloud build remains subject to its actual Actions result.

An incremental overlay requires an existing matching version. Close the application yourself when convenient, back up, replace files and restart. Do not replace DLLs while audio is running. An overlay is not a full distribution.

## Completion and previews

Control Deck switches, sound playback, peak meters, all four gain knobs, layout operations and project restore are implemented. Release builds, offline processing and visible Windows automated checks passed, including global sound shortcuts with another foreground process and four-language display. Physical devices, live JACK connections, installation and human acceptance remain unverified.

**Preview:** DAW Bridge, Carla integration and unfinished main-session audio-device management workflows remain planned or in development. They are not delivered features.

## Themes and licensing

See [Theme customization](Docs/Theme-Customization.md) and the [Development guide](Develop/Development-Guide.md).

First-party sources use [AGPL-3.0](LICENSE). Qt, JACK, VST SDK, FluidSynth, sfizz and BASS/BASSmix retain their respective licenses, supplied with the distribution. See `licenses/bass.txt` and `licenses/bassmix.txt` for the BASS terms.
