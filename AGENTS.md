# Repository memory

## SDK and dependencies

- Qt 6 SDK: `C:\Qt\6.10.3`.
- Windows SDK: `D:\Windows Kits\10`.
- Libjack DLLs: x86 `C:\Windows\libjack.dll`; x64 `C:\Windows\libjack64.dll`.

## Native effects

- Native effects are native modules, not VST plug-ins. Categorize them as `Native Fx`.
- Each effect has eight inputs and eight outputs in this order: `L`, `R`, `C`, `LFE`, `Ls`, `Rs`, `Lrb`, `Rrb`. Controls support both All and per-channel operation.
- “Per-channel control” means each channel retains its own parameter state, not merely its own enable/bypass bit. In All mode, editing a parameter broadcasts the same resulting value to all eight channels and overwrites that parameter's prior per-channel differences; unedited parameters remain independent. Calculate the target from the All display value, then write that absolute target to each channel instead of applying a separate delta to each channel.
- Put shared DSP algorithms in `src/common`; put effect controls and custom-drawn UI in `src/ui`.
- Draw custom curve strokes with `Qt::NoBrush`; draw any filled area as a separate path.
- Keep the Compressor reference window at 770 px wide with its 748 px channel strip and main panel aligned at x=10; preserve narrow, even side margins instead of leaving unused canvas around the controls.
- In Compressor All mode, aggregate active channel levels in the power domain like Graphic EQ; animate the filled area below the response curve rather than drawing per-channel level points. Channel selection is All or one channel at a time.
- An effect's main control interface is a separate top-level window. Do not put the effect controls in the sidebar.
- Keep the effect's channel selector in that same standalone window above the main control panel; do not embed the effect interface in a sidebar.
- Do not create a new build or workspace directory.
- Write one completion document per effect module under `Native Effect`. Mark a module complete only after implementation, build, and tests are complete.

## Build and process rules

- After an intentional JACK port-name change makes connections saved in an existing project stale, ask the user to reconnect the affected ports and save the project. Treat this as the project update step; focus verification on current port naming and connection behavior.
- Follow the repository's existing implementation and verification workflow; use TDD only when explicitly requested.
- Run only tests that work without a running JACK server. Use offline model, module-loading and UI rendering checks for local debugging.
- Finish all intended changes before running one build-and-test verification pass for the task. If that pass finds a failure, fix it and rerun only the necessary verification.
- Build only in the repository's existing `build` directory. Do not create or use an alternate build directory.
- Always compile the Release configuration. Unless the user explicitly requests a full Release build, compile only the targets needed for the Release overlay package and package only its required runtime files.
- Release 覆盖包默认是增量包，只包含本次变更所需的文件。只有在用户明确要求完整包时，才先在现有 `build` 目录完全重编 Release；全量重编成功后方可打包完整包。未经过全量重编的包不得标为完整包。
- For error diagnosis, prioritize available `.dmp` crash dumps and the corresponding timestamped log files to locate the error source. Use that evidence to guide code inspection and any necessary reproduction or tests; when dumps or logs are unavailable or insufficient, proceed with targeted investigation.
- The existing build tree uses the Visual Studio 18 2026 generator. Use its bundled CMake at `D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`; Qt's bundled CMake 3.30 cannot load this generator.
- If MSBuild's C++ task stops with a duplicate `Path`/`PATH` environment-key error, compile with the Visual Studio 18 `cl.exe` using the generated project's compiler flags, then link with `link.exe` using a response file based on that target's `link.command.1.tlog`. Include `/MANIFEST:EMBED` and add `D:\Windows Kits\10\bin\10.0.26100.0\x64` to the process `PATH` so `rc.exe` is available; run the resulting UI test executable from PowerShell to complete verification.
- Keep any running WinJACKNexus instance and its audio processes running during builds and tests; do not terminate them or interrupt the audio stream. If occupied output files block compilation or packaging, report the occupied files and leave the running application intact.
- Run every build or test interactively in the foreground through PowerShell and pipe all output through `2>&1 | Tee-Object -FilePath "build.log"`.
- Immediately capture and check `$LASTEXITCODE`. If it is nonzero, inspect `build.log` before proceeding.
- When compiling VST support, keep `WJN_ENABLE_VST2=ON` and include both VST2 and VST3 targets. Use the existing build tree's `ALL_BUILD` target only when the user explicitly requests a full Release build.
- For Native Fx verification, test module loading and UI behavior only; do not require or run JACK client/port connection integration tests.

## Code navigation

When the repository root has a `.codegraph/` index, use CodeGraph before grep, find, or opening source files to locate or understand code. Use its MCP tool when available, or `codegraph explore "<query>"` from the shell. If there is no index, skip CodeGraph; indexing is the user's decision.

## UI controls and API

- Every project UI refresh timer and animation must follow the application's 30/60/120 FPS setting. Do not add an independent refresh rate for a project-owned view.
- Visible application content controls should come from the WinJACKNexus UI API. Reuse an existing shared control first; when none fits, ask the user whether to create one and whether it belongs in the shared library or as an independent control.
- 所有新增或修改的应用内 UI 控件必须优先复用公共 UI 控件库及既有 Theme Token。禁止在业务窗口、模块或控件中独立硬编码颜色、字体、边框、圆角、控件状态样式或其他可主题化视觉属性，包括局部 QSS/StyleSheet 与自绘代码中的固定视觉值。若现有公共控件或 Token 无法表达需求，应先扩展公共 UI/Theme Token，再由业务界面引用。
- 系统主菜单、托盘菜单、右键菜单和控件弹出菜单继续使用 Qt 原生样式。
- Treat `Develop/Node/Node-API.md`, `Develop/NativeFx/NativeFx-API.md` and `Docs/UI-API.md` as the frozen API contracts. Update the relevant document and call-site tests when an exported signature or behavior changes.
