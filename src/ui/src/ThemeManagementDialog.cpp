/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeManagementDialog.hpp>

#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <QSignalBlocker>
#include <QVariant>
#include "LocalizedThemeMessages.hpp"
#include "LocalizedThemeDialogs.hpp"
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace WinJACKNexus::UI
{
namespace
{
namespace C = Controls;

QString themeText(const char *key, const char *english)
{
    return localizedText("theme", key, english);
}
C::TextLabel *themeLabel(const char *key, const char *english, QWidget *parent)
{
    auto *label = new C::TextLabel({}, parent);
    bindLocalizedText(label, "text", "theme", key, english);
    return label;
}
C::ActionButton *themeButton(const char *key, const char *english, QWidget *parent)
{
    auto *button = new C::ActionButton({}, parent);
    bindLocalizedText(button, "text", "theme", key, english);
    return button;
}
C::ToggleField *themeToggle(const char *key, const char *english, QWidget *parent)
{
    auto *toggle = new C::ToggleField({}, parent);
    bindLocalizedText(toggle, "text", "theme", key, english);
    return toggle;
}
void themeTab(C::TabField *tabs, QWidget *page, const char *key, const char *english)
{
    const auto index = tabs->addTab(page, themeText(key, english));
    if (auto *manager = I18nManager::installed())
        manager->bind(page, [tabs, page, key = QByteArray(key), english = QByteArray(english)] {
            const auto current = tabs->indexOf(page);
            if (current >= 0)
                tabs->setTabText(current, themeText(key.constData(), english.constData()));
        });
    Q_UNUSED(index);
}
void themeChoice(C::ChoiceField *field, const QString &value, const char *key, const char *english)
{
    field->addItem(themeText(key, english), value);
    if (auto *manager = I18nManager::installed())
        manager->bind(field, [field, value, key = QByteArray(key), english = QByteArray(english)] {
            const auto index = field->findData(value);
            if (index < 0)
                return;
            const QSignalBlocker blocker(field);
            field->setItemText(index, themeText(key.constData(), english.constData()));
        });
}
void themeStatus(C::TextLabel *label, const char *key, const char *english)
{
    label->setProperty("wjn.theme.statusCanonical", QVariant{});
    label->setProperty("wjn.theme.statusKey", QByteArray(key));
    label->setProperty("wjn.theme.statusEnglish", QByteArray(english));
    label->setText(themeText(key, english));
}
void themeRawStatus(C::TextLabel *label, const QString &error)
{
    label->setProperty("wjn.theme.statusKey", QByteArray{});
    label->setProperty("wjn.theme.statusEnglish", QByteArray{});
    label->setProperty("wjn.theme.statusCanonical", error);
    label->setText(ThemeMessages::display(error));
}

QString themesDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("themes"));
}

QString token(const QString &scope, const QString &role)
{
    return QStringLiteral("--wjn-theme-%1-background-%2").arg(scope, role);
}

QString cssNumber(double number)
{
    return QString::number(number, 'f', 2);
}

void addChoice(C::ChoiceField *field, const QStringList &values)
{
    for (const auto &value : values)
    {
        if (value == QStringLiteral("original"))
        {
            themeChoice(field, value, "size.original", "Original size");
            continue;
        }
        if (value == QStringLiteral("contain"))
        {
            themeChoice(field, value, "size.contain", "Fit inside");
            continue;
        }
        if (value == QStringLiteral("cover"))
        {
            themeChoice(field, value, "size.cover", "Fill and crop");
            continue;
        }
        if (value == QStringLiteral("stretch"))
        {
            themeChoice(field, value, "size.stretch", "Stretch");
            continue;
        }
        if (value == QStringLiteral("no-repeat"))
        {
            themeChoice(field, value, "repeat.no-repeat", "No tiling");
            continue;
        }
        if (value == QStringLiteral("repeat"))
        {
            themeChoice(field, value, "repeat.repeat", "Tile both axes");
            continue;
        }
        if (value == QStringLiteral("repeat-x"))
        {
            themeChoice(field, value, "repeat.repeat-x", "Tile horizontally");
            continue;
        }
        if (value == QStringLiteral("repeat-y"))
        {
            themeChoice(field, value, "repeat.repeat-y", "Tile vertically");
            continue;
        }
        field->addItem(value, value);
    }
}

void selectValue(C::ChoiceField *field, const QString &value)
{
    const auto index = field->findData(value);
    if (index >= 0)
        field->setCurrentIndex(index);
}
}

struct ThemeManagementDialog::View final
{
    C::TextLabel *identity = nullptr;
    C::ActionButton *homepage = nullptr;
    C::LogField *diagnostics = nullptr;
    C::TextLabel *status = nullptr;
    C::ActionButton *preview = nullptr;
    C::ActionButton *apply = nullptr;
    C::ActionButton *cancel = nullptr;
    C::ChoiceField *colorToken = nullptr;
    C::TextField *colorName = nullptr;
    C::TextField *colorValue = nullptr;
    C::TextLabel *fontName = nullptr;
    C::ChoiceField *backgroundScope = nullptr;
    C::ChoiceField *imageMode = nullptr;
    C::ChoiceField *size = nullptr;
    C::DecimalField *scale = nullptr;
    C::ChoiceField *repeat = nullptr;
    C::TextField *x = nullptr;
    C::TextField *y = nullptr;
    C::DecimalField *imageOpacity = nullptr;
    C::DecimalField *blur = nullptr;
    C::DecimalField *componentOpacity = nullptr;
    C::ToggleField *overlay = nullptr;
    C::DecimalField *overlayOpacity = nullptr;
    C::TextLabel *overlayHint = nullptr;
    C::TextField *name = nullptr;
    C::TextField *author = nullptr;
    C::TextField *description = nullptr;
    C::TextField *homepageEdit = nullptr;
    C::ChoiceField *fragmentScope = nullptr;
};

ThemeManagementDialog::ThemeManagementDialog(ThemeCore &core, QWidget *parent)
    : QDialog(parent), core_(core), view_(std::make_unique<View>())
{
    setObjectName(QStringLiteral("theme.management"));
    bindLocalizedText(this, "windowTitle", "theme", "title", "Theme management");
    resize(860, 680);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new C::TabField(this);
    tabs->setObjectName(QStringLiteral("theme.tabs"));
    layout->addWidget(tabs, 1);

    auto *overview = new QWidget(tabs);
    auto *overviewLayout = new QVBoxLayout(overview);
    view_->identity = new C::TextLabel(overview);
    view_->identity->setTextFormat(Qt::PlainText);
    view_->identity->setWordWrap(true);
    overviewLayout->addWidget(view_->identity);
    view_->homepage = themeButton("homepage.open", "Open author homepage", overview);
    overviewLayout->addWidget(view_->homepage);
    view_->diagnostics = new C::LogField(overview);
    view_->diagnostics->setObjectName(QStringLiteral("theme.diagnostics"));
    view_->diagnostics->setReadOnly(true);
    overviewLayout->addWidget(view_->diagnostics, 1);
    auto *packageActions = new QHBoxLayout;
    auto *importButton = themeButton("package.import", "Import theme package", overview);
    importButton->setObjectName(QStringLiteral("theme.importPackage"));
    view_->preview = themeButton("preview", "Preview", overview);
    view_->apply = themeButton("apply", "Apply", overview);
    view_->cancel = themeButton("preview.cancel", "Cancel preview", overview);
    for (auto *button : {importButton, view_->preview, view_->apply, view_->cancel})
        packageActions->addWidget(button);
    overviewLayout->addLayout(packageActions);
    themeTab(tabs, overview, "tab.overview", "Theme and diagnostics");

    auto *colors = new QWidget(tabs);
    auto *colorLayout = new QFormLayout(colors);
    view_->colorToken = new C::ChoiceField(colors);
    view_->colorToken->setObjectName(QStringLiteral("theme.colorChoice"));
    ThemeRegistry registry;
    ThemeRegistry::builtIn(&registry);
    for (const auto &name : registry.names())
        if (registry.token(name).type == QStringLiteral("color"))
            view_->colorToken->addItem(name, name);
    colorLayout->addRow(themeLabel("color.token", "Color token", colorLayout->parentWidget()),
                        view_->colorToken);
    view_->colorName = new C::TextField(colors);
    view_->colorName->setObjectName(QStringLiteral("theme.colorName"));
    view_->colorName->setPlaceholderText(QStringLiteral("--wjn-theme-..."));
    colorLayout->addRow(themeLabel("color.name", "Full token name", colorLayout->parentWidget()),
                        view_->colorName);
    view_->colorValue = new C::TextField(colors);
    view_->colorValue->setObjectName(QStringLiteral("theme.colorValue"));
    view_->colorValue->setPlaceholderText(QStringLiteral("#RRGGBB / rgb(...) / rgba(...)"));
    colorLayout->addRow(themeLabel("color.value", "Color value", colorLayout->parentWidget()),
                        view_->colorValue);
    auto *setColor = themeButton("color.apply", "Apply color", colors);
    setColor->setObjectName(QStringLiteral("theme.applyColor"));
    colorLayout->addRow(setColor);
    view_->fontName = new C::TextLabel(colors);
    colorLayout->addRow(themeLabel("font.global", "Global font", colorLayout->parentWidget()),
                        view_->fontName);
    auto *fontActions = new QHBoxLayout;
    auto *importFontButton = themeButton("font.import", "Import TTF/OTF", colors);
    auto *systemFontButton = themeButton("font.system", "Restore system font", colors);
    fontActions->addWidget(importFontButton);
    fontActions->addWidget(systemFontButton);
    colorLayout->addRow(fontActions);
    themeTab(tabs, colors, "tab.colors", "Colors and fonts");

    auto *backgrounds = new QWidget(tabs);
    auto *backgroundForm = new QFormLayout(backgrounds);
    view_->backgroundScope = new C::ChoiceField(backgrounds);
    view_->backgroundScope->setObjectName(QStringLiteral("theme.backgroundScope"));
    const QStringList scopes{QStringLiteral("patchbay"),       QStringLiteral("mixer"),
                             QStringLiteral("mixer-overview"), QStringLiteral("sel-main"),
                             QStringLiteral("sel-monitor"),    QStringLiteral("sel-aux"),
                             QStringLiteral("sel-fx"),         QStringLiteral("meter-bridge")};
    for (const auto &scope : scopes)
    {
        if (scope == QStringLiteral("patchbay"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.patchbay", "Patchbay");
            continue;
        }
        if (scope == QStringLiteral("mixer"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.mixer", "Mixer");
            continue;
        }
        if (scope == QStringLiteral("mixer-overview"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.mixer-overview", "Mixer overview");
            continue;
        }
        if (scope == QStringLiteral("sel-main"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.sel-main", "Main mixer");
            continue;
        }
        if (scope == QStringLiteral("sel-monitor"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.sel-monitor", "Monitor mixer");
            continue;
        }
        if (scope == QStringLiteral("sel-aux"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.sel-aux", "Aux mixer");
            continue;
        }
        if (scope == QStringLiteral("sel-fx"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.sel-fx", "FX mixer");
            continue;
        }
        if (scope == QStringLiteral("meter-bridge"))
        {
            themeChoice(view_->backgroundScope, scope, "scope.meter-bridge", "Meter bridge");
            continue;
        }
    }
    backgroundForm->addRow(
        themeLabel("background.scope", "Module region", backgroundForm->parentWidget()),
        view_->backgroundScope);
    view_->imageMode = new C::ChoiceField(backgrounds);
    view_->imageMode->setObjectName(QStringLiteral("theme.imageMode"));
    themeChoice(view_->imageMode, QStringLiteral("inherit"), "image.inherit",
                "Inherit patchbay image");
    themeChoice(view_->imageMode, QStringLiteral("custom"), "image.custom", "Custom image");
    themeChoice(view_->imageMode, QStringLiteral("none"), "image.none", "No image");
    auto *chooseImage = themeButton("image.choose", "Choose image", backgrounds);
    auto *imageRow = new QHBoxLayout;
    imageRow->addWidget(view_->imageMode, 1);
    imageRow->addWidget(chooseImage);
    backgroundForm->addRow(
        themeLabel("image.source", "Image source", backgroundForm->parentWidget()), imageRow);
    view_->size = new C::ChoiceField(backgrounds);
    addChoice(view_->size, {QStringLiteral("original"), QStringLiteral("contain"),
                            QStringLiteral("cover"), QStringLiteral("stretch")});
    backgroundForm->addRow(
        themeLabel("background.size", "Size mode", backgroundForm->parentWidget()), view_->size);
    view_->scale = new C::DecimalField(backgrounds);
    view_->scale->setRange(0.05, 4.0);
    view_->scale->setSingleStep(0.05);
    view_->scale->setDecimals(2);
    backgroundForm->addRow(
        themeLabel("background.scale", "Uniform scale", backgroundForm->parentWidget()),
        view_->scale);
    view_->repeat = new C::ChoiceField(backgrounds);
    addChoice(view_->repeat, {QStringLiteral("no-repeat"), QStringLiteral("repeat"),
                              QStringLiteral("repeat-x"), QStringLiteral("repeat-y")});
    backgroundForm->addRow(
        themeLabel("background.repeat", "Tiling", backgroundForm->parentWidget()), view_->repeat);
    view_->x = new C::TextField(backgrounds);
    view_->y = new C::TextField(backgrounds);
    view_->x->setPlaceholderText(QStringLiteral("50% / 12px"));
    view_->y->setPlaceholderText(QStringLiteral("50% / 12px"));
    backgroundForm->addRow(
        themeLabel("background.x", "Horizontal position", backgroundForm->parentWidget()),
        view_->x);
    backgroundForm->addRow(
        themeLabel("background.y", "Vertical position", backgroundForm->parentWidget()), view_->y);
    const auto opacity = [backgrounds] {
        auto *field = new C::DecimalField(backgrounds);
        field->setRange(0.0, 1.0);
        field->setDecimals(2);
        field->setSingleStep(0.05);
        return field;
    };
    view_->imageOpacity = opacity();
    backgroundForm->addRow(
        themeLabel("image.opacity", "Image opacity", backgroundForm->parentWidget()),
        view_->imageOpacity);
    view_->blur = new C::DecimalField(backgrounds);
    view_->blur->setObjectName(QStringLiteral("theme.blurRadius"));
    view_->blur->setRange(0.0, 64.0);
    view_->blur->setDecimals(0);
    view_->blur->setSuffix(QStringLiteral(" px"));
    backgroundForm->addRow(
        themeLabel("background.blur", "Gaussian blur", backgroundForm->parentWidget()),
        view_->blur);
    view_->componentOpacity = opacity();
    view_->componentOpacity->setObjectName(QStringLiteral("theme.componentOpacity"));
    backgroundForm->addRow(
        themeLabel("component.opacity", "Component opacity", backgroundForm->parentWidget()),
        view_->componentOpacity);
    view_->overlay = themeToggle("overlay.enabled", "Upper overlay", backgrounds);
    view_->overlay->setObjectName(QStringLiteral("theme.upperOverlay"));
    backgroundForm->addRow(themeLabel("overlay.mode", "Layer mode", backgroundForm->parentWidget()),
                           view_->overlay);
    view_->overlayOpacity = opacity();
    backgroundForm->addRow(
        themeLabel("overlay.opacity", "Upper image opacity", backgroundForm->parentWidget()),
        view_->overlayOpacity);
    view_->overlayHint =
        themeLabel("overlay.hint", "Upper overlay: images with transparent backgrounds work best",
                   backgrounds);
    view_->overlayHint->setObjectName(QStringLiteral("theme.overlayHint"));
    backgroundForm->addRow(view_->overlayHint);
    auto *applyBackgroundButton =
        themeButton("background.apply", "Apply background settings", backgrounds);
    applyBackgroundButton->setObjectName(QStringLiteral("theme.applyBackground"));
    backgroundForm->addRow(applyBackgroundButton);
    themeTab(tabs, backgrounds, "tab.background", "Background");

    auto *exports = new QWidget(tabs);
    auto *exportForm = new QFormLayout(exports);
    view_->name = new C::TextField(exports);
    view_->name->setObjectName(QStringLiteral("theme.metadata.name"));
    view_->author = new C::TextField(exports);
    view_->author->setObjectName(QStringLiteral("theme.metadata.author"));
    view_->description = new C::TextField(exports);
    view_->description->setObjectName(QStringLiteral("theme.metadata.description"));
    view_->homepageEdit = new C::TextField(exports);
    view_->homepageEdit->setObjectName(QStringLiteral("theme.metadata.homepage"));
    exportForm->addRow(themeLabel("metadata.name", "Theme name", exportForm->parentWidget()),
                       view_->name);
    exportForm->addRow(themeLabel("metadata.author", "Author", exportForm->parentWidget()),
                       view_->author);
    exportForm->addRow(
        themeLabel("metadata.description", "Description", exportForm->parentWidget()),
        view_->description);
    exportForm->addRow(themeLabel("metadata.homepage", "Homepage", exportForm->parentWidget()),
                       view_->homepageEdit);
    view_->fragmentScope = new C::ChoiceField(exports);
    QSet<QString> exportScopeSet;
    for (const auto &name : registry.names())
        exportScopeSet.insert(registry.token(name).ownerScope);
    const auto &registeredModules = ThemeNamespaceRegistry::instance();
    for (const auto &moduleId : registeredModules.moduleIds())
        if (const auto descriptor = registeredModules.descriptor(moduleId))
            exportScopeSet.insert(
                descriptor->namespacePrefix.mid(12, descriptor->namespacePrefix.size() - 13));
    QStringList exportScopes = exportScopeSet.values();
    exportScopes.sort();
    for (const auto &scope : exportScopes)
        view_->fragmentScope->addItem(scope, QStringLiteral("--wjn-theme-") + scope + '-');
    exportForm->addRow(
        themeLabel("fragment.namespace", "Fragment namespace", exportForm->parentWidget()),
        view_->fragmentScope);
    auto *exportFull = themeButton("export.full", "Export current full theme", exports);
    auto *exportDefault = themeButton("export.default", "Export default theme template", exports);
    auto *exportFragment = themeButton("export.fragment", "Export module fragment", exports);
    exportForm->addRow(exportFull);
    exportForm->addRow(exportDefault);
    exportForm->addRow(exportFragment);
    themeTab(tabs, exports, "tab.export", "Export");

    view_->status = new C::TextLabel(this);
    view_->status->setTextFormat(Qt::PlainText);
    view_->status->setObjectName(QStringLiteral("theme.status"));
    if (auto *manager = I18nManager::installed())
        manager->bind(view_->status, [label = view_->status] {
            const auto key = label->property("wjn.theme.statusKey").toByteArray();
            const auto english = label->property("wjn.theme.statusEnglish").toByteArray();
            if (!key.isEmpty())
                label->setText(themeText(key.constData(), english.constData()));
            else if (label->property("wjn.theme.statusCanonical").isValid())
                label->setText(ThemeMessages::display(
                    label->property("wjn.theme.statusCanonical").toString()));
        });
    layout->addWidget(view_->status);

    const auto activeMeta = core_.activeFullTheme().metadata();
    view_->name->setText(activeMeta.value("name").toString(QStringLiteral("WinJACKNexus Default")));
    view_->author->setText(activeMeta.value("author").toString(QStringLiteral("WinJACKNexus")));
    view_->description->setText(
        activeMeta.value("description").toString(QStringLiteral("Built-in default appearance")));
    view_->homepageEdit->setText(activeMeta.value("homepage").toString());

    connect(importButton, &QPushButton::clicked, this, [this] {
        QDir().mkpath(themesDirectory());
        const auto path = ThemeDialogs::file(this, false, themesDirectory(), "package.import",
                                             "Import theme package", "file.themeFilter",
                                             "WinJACKNexus themes (*.wjnskin)");
        QString error;
        if (!path.isEmpty() && !openPackage(path, &error))
            themeRawStatus(view_->status, error);
    });
    connect(view_->preview, &QPushButton::clicked, this, [this] {
        QString error;
        if (!preview(&error))
            themeRawStatus(view_->status, error);
    });
    connect(view_->apply, &QPushButton::clicked, this, [this] {
        QString error;
        if (!apply(&error))
            themeRawStatus(view_->status, error);
    });
    connect(view_->cancel, &QPushButton::clicked, this, [this] { cancelPreview(); });
    connect(view_->homepage, &QPushButton::clicked, this, [this] {
        const auto meta =
            candidate_.isValid() ? candidate_.metadata() : core_.activeFullTheme().metadata();
        const auto link = meta.value("homepage").toString();
        if (!link.isEmpty())
            QDesktopServices::openUrl(QUrl(link));
    });
    connect(view_->colorToken, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const auto name = view_->colorToken->currentData().toString();
        view_->colorName->setText(name);
        view_->colorValue->setText(core_.snapshot()->value(name));
    });
    connect(setColor, &QPushButton::clicked, this, [this] {
        const auto enteredName = view_->colorName->text().trimmed();
        const auto name =
            enteredName.isEmpty() ? view_->colorToken->currentData().toString() : enteredName;
        const auto value = view_->colorValue->text().trimmed();
        QString error;
        if (!ThemeRegistry::valueMatchesType(QStringLiteral("color"), value) ||
            !core_.setOverride(name, value, {}, &error))
        {
            if (error.isEmpty())
                themeStatus(view_->status, "error.color", "Invalid color value format");
            else
                themeRawStatus(view_->status, error);
        }
        else
            themeStatus(view_->status, "status.colorApplied", "Color applied");
    });
    connect(importFontButton, &QPushButton::clicked, this, [this] {
        const auto path = ThemeDialogs::file(this, false, {}, "file.fontTitle", "Choose font",
                                             "file.fontFilter", "Fonts (*.ttf *.otf)");
        QString error;
        if (!path.isEmpty() && !importFont(path, &error))
            themeRawStatus(view_->status, error);
    });
    connect(systemFontButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!core_.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                               QStringLiteral("system"), {}, &error))
            themeRawStatus(view_->status, error);
    });
    connect(view_->backgroundScope, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { refreshBackgroundEditor(); });
    connect(view_->overlay, &QCheckBox::toggled, this, [this](bool enabled) {
        view_->overlayHint->setVisible(enabled);
        view_->blur->setEnabled(!enabled);
        view_->componentOpacity->setEnabled(!enabled);
        view_->overlayOpacity->setEnabled(enabled);
    });
    connect(chooseImage, &QPushButton::clicked, this, [this] {
        const auto path =
            ThemeDialogs::file(this, false, {}, "file.imageTitle", "Choose background image",
                               "file.imageFilter", "Images (*.png *.jpg *.jpeg *.webp)");
        QString error;
        if (!path.isEmpty() && !importImage(path, &error))
            themeRawStatus(view_->status, error);
    });
    connect(applyBackgroundButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!applyBackground(&error))
            themeRawStatus(view_->status, error);
        else
            themeStatus(view_->status, "status.backgroundApplied", "Background settings applied");
    });
    const auto exportAction = [this](const QString &scope, bool builtIn) {
        QDir().mkpath(themesDirectory());
        const auto suggested =
            QDir(themesDirectory())
                .filePath(builtIn           ? QStringLiteral("winjacknexus-default.wjnskin")
                          : scope.isEmpty() ? QStringLiteral("custom.wjnskin")
                                            : QStringLiteral("module-%1.wjnskin")
                                                  .arg(scope.mid(12, scope.size() - 13)));
        const auto path =
            ThemeDialogs::file(this, true, suggested, "file.exportTitle", "Export theme",
                               "file.themeFilter", "WinJACKNexus themes (*.wjnskin)");
        QString error;
        if (!path.isEmpty() && !exportToFile(path, scope, builtIn, &error))
            themeRawStatus(view_->status, error);
        else if (!path.isEmpty())
            themeStatus(view_->status, "status.exported", "Theme package exported");
    };
    connect(exportFull, &QPushButton::clicked, this, [exportAction] { exportAction({}, false); });
    connect(exportDefault, &QPushButton::clicked, this, [exportAction] { exportAction({}, true); });
    connect(exportFragment, &QPushButton::clicked, this, [this, exportAction] {
        exportAction(view_->fragmentScope->currentData().toString(), false);
    });
    connect(&core_, &ThemeCore::themeChanged, this, [this] { refresh(); });
    view_->colorName->setText(view_->colorToken->currentData().toString());
    view_->colorValue->setText(core_.snapshot()->value(view_->colorName->text()));
    refreshBackgroundEditor();
    refresh();
    if (auto *manager = I18nManager::installed())
        manager->bind(this, [this] {
            refreshLocalizedIdentity();
            refreshLocalizedDiagnostics();
        });
}

ThemeManagementDialog::~ThemeManagementDialog() = default;

bool ThemeManagementDialog::openPackage(const QString &path, QString *error)
{
    ThemePackage next;
    if (!ThemePackage::loadFromFile(path, &next, error))
        return false;
    if (previewing_)
        cancelPreview();
    candidate_ = next;
    candidatePath_ = QFileInfo(path).absoluteFilePath();
    const auto meta = candidate_.metadata();
    view_->name->setText(meta.value("name").toString());
    view_->author->setText(meta.value("author").toString());
    view_->description->setText(meta.value("description").toString());
    view_->homepageEdit->setText(meta.value("homepage").toString());
    refresh();
    return true;
}

bool ThemeManagementDialog::preview(QString *error)
{
    if (!candidate_.isValid() || candidate_.metadata().value("kind") != "full")
        return false;
    if (previewing_)
        return true;
    if (!core_.beginPreview(candidate_, error))
        return false;
    previewing_ = true;
    refresh();
    return true;
}

bool ThemeManagementDialog::apply(QString *error)
{
    if (!candidate_.isValid())
        return false;
    const bool full = candidate_.metadata().value("kind") == "full";
    if (full)
    {
        if (!previewing_)
        {
            ThemeCore probe;
            if (!probe.setFullTheme(candidate_, error))
                return false;
        }
        QDir folder(themesDirectory());
        if (!folder.exists() && !QDir().mkpath(folder.absolutePath()))
        {
            if (error)
                *error = ThemeMessages::canonical("Cannot create theme directory");
            return false;
        }
        QString destination = folder.filePath(candidate_.id() + QStringLiteral(".wjnskin"));
        for (const auto &file : folder.entryInfoList(QDir::Files | QDir::NoSymLinks))
        {
            if (file.suffix().compare(QStringLiteral("wjnskin"), Qt::CaseInsensitive) != 0)
                continue;
            ThemePackage existing;
            if (ThemePackage::loadFromFile(file.absoluteFilePath(), &existing) &&
                existing.id() == candidate_.id())
            {
                destination = file.absoluteFilePath();
                break;
            }
        }
        if (QFileInfo(candidatePath_).absoluteFilePath() !=
                QFileInfo(destination).absoluteFilePath() &&
            !candidate_.saveToFile(destination, error))
            return false;
    }
    if (previewing_)
    {
        if (!core_.commitPreview())
            return false;
        previewing_ = false;
    }
    else if (candidate_.metadata().value("kind") == "fragment")
    {
        if (!core_.applyFragment(candidate_, error))
            return false;
    }
    else if (!core_.setFullTheme(candidate_, error))
        return false;
    candidate_ = {};
    candidatePath_.clear();
    refresh();
    return true;
}

bool ThemeManagementDialog::cancelPreview()
{
    if (!previewing_)
        return false;
    previewing_ = false;
    const auto restored = core_.cancelPreview();
    refresh();
    return restored;
}

QJsonObject ThemeManagementDialog::editedMetadata() const
{
    return {{"name", view_->name->text().trimmed()},
            {"author", view_->author->text().trimmed()},
            {"description", view_->description->text().trimmed()},
            {"homepage", view_->homepageEdit->text().trimmed()}};
}

bool ThemeManagementDialog::exportToFile(const QString &path, const QString &namespacePrefix,
                                         bool builtInDefault, QString *error) const
{
    ThemePackage exported;
    if (builtInDefault)
    {
        ThemeCore defaults;
        if (!defaults.exportFullTheme({}, &exported, error))
            return false;
    }
    else if (namespacePrefix.isEmpty())
    {
        if (!core_.exportFullTheme(editedMetadata(), &exported, error))
            return false;
    }
    else if (!core_.exportFragment(namespacePrefix, editedMetadata(), &exported, error))
    {
        return false;
    }
    return exported.saveToFile(path, error);
}

bool ThemeManagementDialog::importImage(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
    {
        if (error)
            *error = ThemeMessages::canonical(
                "Background image exceeds the size limit or cannot be read");
        return false;
    }
    QImageReader reader(&file);
    if (!reader.canRead())
    {
        if (error)
            *error = ThemeMessages::canonical("Cannot read background image");
        return false;
    }
    file.seek(0);
    const auto extension = QFileInfo(path).suffix().toLower();
    const auto logical = QStringLiteral("assets/imported-background.%1").arg(extension);
    const auto scope = view_->backgroundScope->currentData().toString();
    const QJsonObject meta{
        {"formatVersion", 1},
        {"themeApiVersion", 1},
        {"id", "org.winjacknexus-author-image"},
        {"name", "Imported image"},
        {"version", "1.0.0"},
        {"author", "WinJACKNexus"},
        {"description", "Temporary image resource for theme export"},
        {"homepage", ""},
        {"kind", "full"},
        {"namespaces", QJsonArray{QStringLiteral("--wjn-theme-") + scope + '-'}}};
    ThemePackage package;
    if (!ThemePackage::create(meta, ":root {}", {{logical, file.readAll()}}, &package, error))
        return false;
    importedImages_.insert(scope, package);
    selectValue(view_->imageMode, QStringLiteral("custom"));
    return true;
}

bool ThemeManagementDialog::importFont(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
    {
        if (error)
            *error = ThemeMessages::canonical("Font file exceeds the size limit or cannot be read");
        return false;
    }
    const auto bytes = file.readAll();
    if (QFontDatabase::addApplicationFontFromData(bytes) < 0)
    {
        if (error)
            *error = ThemeMessages::canonical("Invalid font file");
        return false;
    }
    const auto logical =
        QStringLiteral("assets/imported-font.%1").arg(QFileInfo(path).suffix().toLower());
    const QJsonObject meta{{"formatVersion", 1},
                           {"themeApiVersion", 1},
                           {"id", "org.winjacknexus-author-font"},
                           {"name", "Imported font"},
                           {"version", "1.0.0"},
                           {"author", "WinJACKNexus"},
                           {"description", "Temporary font resource for theme export"},
                           {"homepage", ""},
                           {"kind", "full"},
                           {"namespaces", QJsonArray{"--wjn-theme-global-"}}};
    ThemePackage package;
    if (!ThemePackage::create(meta, ":root {}", {{logical, bytes}}, &package, error))
        return false;
    return core_.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                             QStringLiteral("url(\"%1\")").arg(logical), package, error);
}

bool ThemeManagementDialog::applyBackground(QString *error)
{
    const auto scope = view_->backgroundScope->currentData().toString();
    const auto mode = view_->imageMode->currentData().toString();
    const auto size = view_->size->currentData().toString();
    const auto repeat = view_->repeat->currentData().toString();
    const auto x = view_->x->text().trimmed();
    const auto y = view_->y->text().trimmed();
    const auto scale = cssNumber(view_->scale->value());
    const auto imageOpacity = cssNumber(view_->imageOpacity->value());
    const auto blur = QString::number(view_->blur->value(), 'f', 0) + QStringLiteral("px");
    const auto componentOpacity = cssNumber(view_->componentOpacity->value());
    const auto overlay = view_->overlay->isChecked();
    const auto overlayOpacity = cssNumber(view_->overlayOpacity->value());
    if (!ThemeRegistry::valueMatchesType(QStringLiteral("position"), x) ||
        !ThemeRegistry::valueMatchesType(QStringLiteral("position"), y))
    {
        if (error)
            *error = ThemeMessages::canonical("Position must be a percentage or px value");
        return false;
    }
    if (mode == "none")
    {
        if (!core_.setOverride(token(scope, QStringLiteral("image")), QStringLiteral("none"), {},
                               error))
            return false;
    }
    else if (mode == "inherit")
    {
        if (scope == QStringLiteral("patchbay"))
            core_.removeOverride(token(scope, QStringLiteral("image")));
        else if (!core_.setOverride(token(scope, QStringLiteral("image")),
                                    QStringLiteral("var(--wjn-theme-patchbay-background-image)"),
                                    {}, error))
            return false;
    }
    else
    {
        const auto imported = importedImages_.value(scope);
        if (imported.isValid())
        {
            if (!core_.setOverride(
                    token(scope, QStringLiteral("image")),
                    QStringLiteral("url(\"%1\")").arg(imported.resourceNames().first()), imported,
                    error))
                return false;
        }
        else if (!core_.declaredValue(token(scope, QStringLiteral("image"))).startsWith("url("))
        {
            if (error)
                *error = ThemeMessages::canonical("Choose a custom image first");
            return false;
        }
    }
    const QList<QPair<QString, QString>> values{
        {QStringLiteral("size"), size},
        {QStringLiteral("scale"), scale},
        {QStringLiteral("repeat"), repeat},
        {QStringLiteral("position-x"), x},
        {QStringLiteral("position-y"), y},
        {QStringLiteral("opacity"), imageOpacity},
        {QStringLiteral("blur-radius"), blur},
        {QStringLiteral("component-opacity"), componentOpacity},
        {QStringLiteral("overlay-enabled"),
         overlay ? QStringLiteral("true") : QStringLiteral("false")},
        {QStringLiteral("overlay-opacity"), overlayOpacity}};
    for (const auto &[role, value] : values)
        if (!core_.setOverride(token(scope, role), value, {}, error))
            return false;
    return true;
}

void ThemeManagementDialog::refreshBackgroundEditor()
{
    const auto snapshot = core_.snapshot();
    if (!snapshot)
        return;
    const auto scope = view_->backgroundScope->currentData().toString();
    const auto value = [&](const QString &role) { return snapshot->value(token(scope, role)); };
    const auto declaredImage = core_.declaredValue(token(scope, QStringLiteral("image")));
    if (declaredImage == QStringLiteral("none") ||
        (scope == QStringLiteral("patchbay") && declaredImage.isEmpty()))
        selectValue(view_->imageMode, QStringLiteral("none"));
    else if (importedImages_.contains(scope) || declaredImage.startsWith("url("))
        selectValue(view_->imageMode, QStringLiteral("custom"));
    else
        selectValue(view_->imageMode, QStringLiteral("inherit"));
    selectValue(view_->size, value(QStringLiteral("size")));
    selectValue(view_->repeat, value(QStringLiteral("repeat")));
    view_->scale->setValue(value(QStringLiteral("scale")).toDouble());
    view_->x->setText(value(QStringLiteral("position-x")));
    view_->y->setText(value(QStringLiteral("position-y")));
    view_->imageOpacity->setValue(value(QStringLiteral("opacity")).toDouble());
    view_->blur->setValue(value(QStringLiteral("blur-radius")).chopped(2).toDouble());
    view_->componentOpacity->setValue(value(QStringLiteral("component-opacity")).toDouble());
    view_->overlayOpacity->setValue(value(QStringLiteral("overlay-opacity")).toDouble());
    view_->overlay->setChecked(value(QStringLiteral("overlay-enabled")) == QStringLiteral("true"));
    view_->overlayHint->setVisible(view_->overlay->isChecked());
    view_->blur->setEnabled(!view_->overlay->isChecked());
    view_->componentOpacity->setEnabled(!view_->overlay->isChecked());
    view_->overlayOpacity->setEnabled(view_->overlay->isChecked());
}

void ThemeManagementDialog::refreshLocalizedIdentity()
{
    const auto meta =
        candidate_.isValid() ? candidate_.metadata() : core_.activeFullTheme().metadata();
    const auto name = meta.value("name").toString(QStringLiteral("WinJACKNexus Default"));
    const auto author = meta.value("author").toString(QStringLiteral("WinJACKNexus"));
    const auto description =
        meta.value("id").toString() == core_.defaultTheme().id()
            ? themeText("identity.defaultDescription", "Built-in default appearance")
            : meta.value("description").toString();
    const auto homepage = meta.value("homepage").toString();
    view_->identity->setText(
        themeText("identity.summary", "%1\nAuthor: %2\n%3\nHomepage: %4")
            .arg(name, author, description,
                 homepage.isEmpty() ? themeText("identity.noHomepage", "Not provided") : homepage));
    view_->homepage->setEnabled(!homepage.isEmpty());
}

void ThemeManagementDialog::refreshLocalizedDiagnostics()
{
    QStringList lines;
    for (const auto &item : core_.snapshot()->diagnostics())
        lines.append(QStringLiteral("%1:%2 %3 %4")
                         .arg(item.line)
                         .arg(item.code, item.token, ThemeMessages::display(item.message)));
    const auto text = lines.join('\n');
    if (view_->diagnostics->toPlainText() == text)
        return;
    const auto oldCursor = view_->diagnostics->textCursor();
    const auto scroll = view_->diagnostics->verticalScrollBar()->value();
    view_->diagnostics->setPlainText(text);
    auto cursor = view_->diagnostics->textCursor();
    const auto end = view_->diagnostics->document()->characterCount() - 1;
    cursor.setPosition(std::min(oldCursor.anchor(), end));
    cursor.setPosition(std::min(oldCursor.position(), end), QTextCursor::KeepAnchor);
    view_->diagnostics->setTextCursor(cursor);
    view_->diagnostics->verticalScrollBar()->setValue(scroll);
}

void ThemeManagementDialog::refresh()
{
    ThemeManager themeAdapter;
    themeAdapter.applyToWidget(this);
    refreshLocalizedIdentity();
    view_->preview->setEnabled(candidate_.isValid() &&
                               candidate_.metadata().value("kind") == "full" && !previewing_);
    view_->apply->setEnabled(candidate_.isValid());
    view_->cancel->setEnabled(previewing_);
    refreshLocalizedDiagnostics();
    view_->fontName->setText(
        core_.snapshot()->value(QStringLiteral("--wjn-theme-global-font-source")));
    refreshBackgroundEditor();
}

void ThemeManagementDialog::closeEvent(QCloseEvent *event)
{
    if (previewing_)
        cancelPreview();
    QDialog::closeEvent(event);
}

}
