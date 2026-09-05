## VST2/VST3 支持计划书编写提示词
###  所需节点
**内部分类名：** `VST Support`
**单VST节点：** `nodes/vst/mod_vst_host.dll`，
**VST链节点：** `nodes/vst/mod_chain_host.dll`，vst 特化子 `pathbay` 节点，可选组件比主界面额外多出 chain in/out 端口，用于接通主界面，单独窗口，插件通过 `mod_vst_host.dll` 加载，布局参考主 patchbay
**额外专用节点分类栏：** 根据文件夹分类，识别 `.vstinfo/.vst3info` 这个扩展名，`Json` 格式，一个插件一个  `.vstinfo/.vst3.info`

参考结构：
 - VSTPlugins
    - VST2
        - Instruments
            - *.vstinfo
        - Effects
            - EQ
                - *.vstinfo
            - Dynamic
                - *.vstinfo
            - etc.
    - VST3
        - Instruments
            - *.vst3info
        - Effects
            - EQ
                - *.vst3info
            - Dynamic
                - *.vst3info
            - etc.
### 需求
1. 支持VST2/VST3；
2. mod_vst_host 可以侧边栏设置插件路径，可以同步插件选择的预设，端口显示方式（默认，自定义总线/标签名，标准声道名，jack默认），可以`打开自带GUI`，自带GUI上面有个预设栏，可以直接保存和加载`.fbx`预设，菜单和侧边设置栏添加`重载VST`，非VST节点隐藏这个选项；节点 DLL、设置面板和编辑器容器保持使用 Qt；
3. 可以存档（包括插件路径，插件参数，当前预设，客户端名）；
4. 独立沙盒进程运行VST，可以保证如果插件崩溃，对应节点旁通，而不是崩掉整个 WinJACKNexus。
### 所需组件
1. **插件扫描器：** 独立 `Vst Scanner.exe`，可以设置多个VST路径，扫描完成后自动写入 `nodes/VSTPlugins` 对应目录，如果是 `WAVES Shell` 这种，`.vstinfo/.vst3info` 对应插件要有额外的 `internal_plugin` 字段，使用 `Qt6`
2. **插件处理器：** 独立 `VSTProcess32.exe/VSTProcess64.exe`，命令行加载对应插件，识别通用路径 `--path` 和额外的 `--internal_plugin` (用于 Waves Shell 这种)，使用原生 C++/Win32 + VST2/VST3 SDK，不依赖 Qt/JUCE，通过 IPC 与 Qt 节点通信。
### 端口
1. **端口名 fallback 规则：** 自定义总线/标签名 → jack默认；
2. **其他：** 端口数量取决于插件能力，包括音频和midi。

### 客户端名
1. **名字获取：**
    - **普通插件：** 无扩展名插件文件名
    - **Shell：** 内部插件名
2. **过长插件名 fallback 规则：** 完整名 → 中间省略 → 缩写 → vst_xxxxx 随机后缀
3. **重名规则：** 上述名字_自增序号