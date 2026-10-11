// S6 actual module and scanner-entry checks. Included in the real-window host test.
void deliveryChecks(const QString &runtime)
{
    const auto modules = scanNodeModules();
    unsigned independent = 0;
    for (const auto &module : modules)
    {
        if (module.capabilities != (WJN_NODE_JACK_NODE | WJN_NODE_VIRT_NODE))
            continue;
        const auto library = LoadLibraryW(reinterpret_cast<LPCWSTR>(module.modulePath.utf16()));
        require(library != nullptr, "actual dual-mode module loads for state isolation");
        const auto get = reinterpret_cast<const char *(*)(WinJACKNexus::Node::INode *)>(
            GetProcAddress(library, "wjn_get_node_settings_json"));
        const auto set = reinterpret_cast<int (*)(WinJACKNexus::Node::INode *, const char *)>(
            GetProcAddress(library, "wjn_set_node_settings_json"));
        const auto destroy = reinterpret_cast<decltype(&wjn_destroy_node)>(
            GetProcAddress(library, "wjn_destroy_node"));
        require(get && set && destroy, "actual module has settings round-trip exports");
        auto *jack = WinJACKNexus::Node::createNodeForMode(library, WJN_NODE_MODE_JACK);
        auto *virt = WinJACKNexus::Node::createNodeForMode(library, WJN_NODE_MODE_VIRT);
        require(jack && virt, "same module creates independent explicit modes");
        const QByteArray originalJack(get(jack));
        auto changed = QJsonDocument::fromJson(QByteArray(get(virt))).object();
        changed["clientName"] = QStringLiteral("S6 independent virtual instance");
        const auto state = QJsonDocument(changed).toJson(QJsonDocument::Compact);
        require(set(virt, state.constData()) == 1 && QByteArray(get(jack)) == originalJack,
                "changing VIRT settings preserves simultaneous JACK instance state");
        const QByteArray originalVirt(get(virt));
        require(originalVirt != originalJack && set(virt, originalVirt.constData()) == 1 &&
                    QByteArray(get(virt)) == originalVirt && QByteArray(get(jack)) == originalJack,
                "actual dual-mode settings round-trip preserves independent values");
        destroy(virt);
        destroy(jack);
        FreeLibrary(library);
        ++independent;
    }
    require(independent == 11, "all eleven built-in dual-mode modules checked individually");

    // Use the scanner's persisted schema, discovered through the actual host call site.
    const auto directory = runtime + "/nodes/VSTPlugins/prepublic-s6-fixtures";
    require(!QDir(directory).exists() && QDir().mkpath(directory),
            "scanner fixture directory is new and test-owned");
    for (const auto bits : {32, 64})
        for (const auto type : {2, 3})
        {
            const auto name = QString("S6 VST%1 x%2").arg(type).arg(bits);
            QFile file(directory + '/' + name + (type == 2 ? ".vstinfo" : ".vst3info"));
            const QJsonObject record{
                {"name", name},
                {"type", QString("vst%1").arg(type)},
                {"bits", bits},
                {"verified", true},
                {"internal_plugin", type == 3 ? "7856341222114433556677881234ABCD" : ""},
                {"path", QString("%1/tests/vst-s5/Vst%2BlockFixture%3.%4")
                             .arg(runtime)
                             .arg(type)
                             .arg(bits)
                             .arg(type == 2 ? "dll" : "vst3")}};
            require(file.open(QIODevice::WriteOnly) &&
                        file.write(QJsonDocument(record).toJson()) > 0,
                    "write architecture-specific scanner record");
        }
    for (const auto mode : {WJN_NODE_MODE_JACK, WJN_NODE_MODE_VIRT})
    {
        QTreeWidget tree;
        tree.setColumnCount(1);
        addVstPluginItems(&tree, modules, mode);
        unsigned entries = 0;
        QTreeWidgetItemIterator iterator(&tree);
        while (*iterator)
        {
            auto *item = *iterator++;
            if (!item->text(0).startsWith("S6 VST"))
                continue;
            const auto configured = item->data(0, Qt::UserRole + 4).toString();
            const auto identity = QJsonDocument::fromJson(configured.toUtf8()).object();
            require(item->toolTip(0).contains("JACK") && item->toolTip(0).contains("GUI") &&
                        identity.value("bits").toString() != "",
                    "scanner entry inherits Host capabilities and persisted identity");
            if (mode == WJN_NODE_MODE_VIRT)
            {
                auto node = LoadedModuleNode::create(item->data(0, Qt::UserRole).toString(), 0,
                                                     configured.toStdString(), mode, 48000, 64);
                require(node && node->isVirtual() && node->virtualPorts().size() == 5,
                        "actual scanned plugin entry prepares correct VIRT worker and ports");
            }
            ++entries;
        }
        require(entries == 4, "four scanned VST combinations appear in each graph context");
    }
    for (const auto &name : QDir(directory).entryList(QDir::Files))
        require(QFile::remove(directory + '/' + name), "remove test-owned scanner record");
    require(QDir().rmdir(directory), "remove empty scanner fixture directory");
}
