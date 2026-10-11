# MIDI Tools dependency/source notices

FluidSynth 2.5.7 is distributed as `libfluidsynth-3.dll` under its supplied
LGPL-2.1-or-later terms. The package includes the exact upstream source archive
(SHA256 `ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a`),
LICENSE, and the application-side `Engines.cmake` configuration and patch.
The only FluidSynth patch replaces `alloca(_len * sizeof(_type))` with
`alloca((_len) * sizeof(_type))` in `src/utils/fluidsynth_priv.h`.

Rebuild with Visual Studio 18 C++, Windows SDK and CMake, using the supplied
source archive and configuration: `osal=cpp11`, shared library, Release DLL CRT;
all driver/network/DLS/libsndfile/OpenMP features listed in Engines.cmake are
OFF. Apply the one-line patch before building the `libfluidsynth` target.
The full WinJACKNexus source can also be configured in its existing build tree,
then `cmake --build build --config Release --target libfluidsynth` performs the
same build. Preserve the DLL name and exported FluidSynth API when replacing
the library beside the application. Stop your own application before replacement;
no user settings or projects need to change. The binary uses dynamic linkage,
so a compatible modified DLL may be substituted without relinking the node.

sfizz 1.2.3 is distributed as `sfizz.dll` with its BSD-2-Clause license and exact
complete upstream source archive (SHA256
`a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822`).
Its included dependencies retain their original notices, including Abseil,
atomic_queue, Cephes, cpuid, filesystem, fmidi, jsl, kiss_fft, pugixml, simde,
spline, st_audiofile/dr_libs/libaiff/stb_vorbis/WavPack and tunings.
Some archive dependencies are unused by the disabled tool/test targets; their
notices are retained as a conservative complete upstream inventory.

GCEM commit `012ae73c6d0a2cb09ffe86475f5c6fba3926e200` is header-only;
the supplied source ZIP SHA256 is
`28159274c54e9640354852e172d10d88eb159f4e7f2fea42edbcd20105ed3526`.
Its LICENSE and NOTICE are retained. No sample banks are bundled with the
runtime. The independent mathematical-tone test fixtures are CC0 and remain
development fixtures.

All license files retain upstream text and directory paths under
`licenses/midi-tools/`. Read those terms for each component. WinJACKNexus remains
AGPL-3.0-only. Project source is available at
https://github.com/AI-Hobbyist/WinJACKNexus .
