# Native Fx development

When adding or changing an effect, read [the API contract](../../Develop/NativeFx/NativeFx-API.md)
and its metadata before editing the host.

1. Create one `effects/<id>/` folder per DLL. Keep `Meta.hpp` at its root,
   `Ui.hpp`, `Logic.inc`, `Dsp.hpp` under `include/`, and `Ui.cpp`, `Dsp.cpp`
   under `src/`. Set the mixer and node placement bits in its stable
   metadata record. Completion criterion: both hosts discover only placements
   declared there, and a mixer-only effect reports zero nodes. Node placement
   exports the boundary v1 functions and validates the eight registered JACK
   inputs and outputs after initialization.
2. Keep each effect's DSP algorithm and formulas in its `Dsp.cpp` and
   per-channel parameter state in its module. Use the Native Fx mixer instance
   for host-owned routing and the node adapter for JACK routing.
   Completion criterion: the mixer instance can be created and edited without
   opening a JACK client; the node has a standalone GUI with its channel
   selector, and the patchbay supplies its client-name-only right sidebar.
3. Put reusable effect control shapes in `src/ui/Controls.cpp`, call them from
   each effect's `Ui.cpp` with a `Controls::Palette`, and register custom QWidget
   factories in a plugin-owned `Controls::ControlRegistry`. Completion
   criterion: color overrides reach custom-drawn controls, the mixer editor
   sits under the chain selector, and the node editor retains its selector.
4. Build in the existing `build` tree and test module loading and UI behavior.
   Update one completion document for the changed effect under `Native Effect`
   only after implementation, build and tests pass.
