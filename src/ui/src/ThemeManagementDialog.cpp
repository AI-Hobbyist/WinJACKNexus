/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/ThemeManagementDialog.hpp>

#include <WinJACKNexus/UI/Controls.hpp>
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

namespace WinJACKNexus::UI {
namespace {
namespace C = Controls;

QString themesDirectory() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("themes"));
}

QString token(const QString& scope, const QString& role) {
    return QStringLiteral("--wjn-theme-%1-background-%2").arg(scope, role);
}

QString cssNumber(double number) {
    return QString::number(number, 'f', 2);
}

void addChoice(C::ChoiceField* field, const QStringList& values) {
    for (const auto& value : values) field->addItem(value, value);
}

void selectValue(C::ChoiceField* field, const QString& value) {
    const auto index = field->findData(value);
    if (index >= 0) field->setCurrentIndex(index);
}
}

struct ThemeManagementDialog::View final {
    C::TextLabel* identity = nullptr;
    C::ActionButton* homepage = nullptr;
    C::LogField* diagnostics = nullptr;
    C::TextLabel* status = nullptr;
    C::ActionButton* preview = nullptr;
    C::ActionButton* apply = nullptr;
    C::ActionButton* cancel = nullptr;
    C::ChoiceField* colorToken = nullptr;
    C::TextField* colorName = nullptr;
    C::TextField* colorValue = nullptr;
    C::TextLabel* fontName = nullptr;
    C::ChoiceField* backgroundScope = nullptr;
    C::ChoiceField* imageMode = nullptr;
    C::ChoiceField* size = nullptr;
    C::DecimalField* scale = nullptr;
    C::ChoiceField* repeat = nullptr;
    C::TextField* x = nullptr;
    C::TextField* y = nullptr;
    C::DecimalField* imageOpacity = nullptr;
    C::DecimalField* blur = nullptr;
    C::DecimalField* componentOpacity = nullptr;
    C::ToggleField* overlay = nullptr;
    C::DecimalField* overlayOpacity = nullptr;
    C::TextLabel* overlayHint = nullptr;
    C::TextField* name = nullptr;
    C::TextField* author = nullptr;
    C::TextField* description = nullptr;
    C::TextField* homepageEdit = nullptr;
    C::ChoiceField* fragmentScope = nullptr;
};

ThemeManagementDialog::ThemeManagementDialog(ThemeCore& core, QWidget* parent)
    : QDialog(parent), core_(core), view_(std::make_unique<View>()) {
    setObjectName(QStringLiteral("theme.management"));
    setWindowTitle(QStringLiteral("主题管理"));
    resize(860, 680);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new C::TabField(this);
    tabs->setObjectName(QStringLiteral("theme.tabs"));
    layout->addWidget(tabs, 1);

    auto* overview = new QWidget(tabs);
    auto* overviewLayout = new QVBoxLayout(overview);
    view_->identity = new C::TextLabel(overview);
    view_->identity->setWordWrap(true);
    overviewLayout->addWidget(view_->identity);
    view_->homepage = new C::ActionButton(QStringLiteral("打开作者主页"), overview);
    overviewLayout->addWidget(view_->homepage);
    view_->diagnostics = new C::LogField(overview);
    view_->diagnostics->setObjectName(QStringLiteral("theme.diagnostics"));
    view_->diagnostics->setReadOnly(true);
    overviewLayout->addWidget(view_->diagnostics, 1);
    auto* packageActions = new QHBoxLayout;
    auto* importButton = new C::ActionButton(QStringLiteral("导入主题包"), overview);
    view_->preview = new C::ActionButton(QStringLiteral("预览"), overview);
    view_->apply = new C::ActionButton(QStringLiteral("应用"), overview);
    view_->cancel = new C::ActionButton(QStringLiteral("取消预览"), overview);
    for (auto* button : {importButton, view_->preview, view_->apply, view_->cancel})
        packageActions->addWidget(button);
    overviewLayout->addLayout(packageActions);
    tabs->addTab(overview, QStringLiteral("主题与诊断"));

    auto* colors = new QWidget(tabs);
    auto* colorLayout = new QFormLayout(colors);
    view_->colorToken = new C::ChoiceField(colors);
    view_->colorToken->setObjectName(QStringLiteral("theme.colorChoice"));
    ThemeRegistry registry;
    ThemeRegistry::builtIn(&registry);
    for (const auto& name : registry.names())
        if (registry.token(name).type == QStringLiteral("color"))
            view_->colorToken->addItem(name, name);
    colorLayout->addRow(QStringLiteral("颜色 token"), view_->colorToken);
    view_->colorName = new C::TextField(colors);
    view_->colorName->setPlaceholderText(QStringLiteral("--wjn-theme-..."));
    colorLayout->addRow(QStringLiteral("完整 token 名"), view_->colorName);
    view_->colorValue = new C::TextField(colors);
    view_->colorValue->setObjectName(QStringLiteral("theme.colorValue"));
    view_->colorValue->setPlaceholderText(QStringLiteral("#RRGGBB / rgb(...) / rgba(...)"));
    colorLayout->addRow(QStringLiteral("颜色值"), view_->colorValue);
    auto* setColor = new C::ActionButton(QStringLiteral("应用颜色"), colors);
    colorLayout->addRow(setColor);
    view_->fontName = new C::TextLabel(colors);
    colorLayout->addRow(QStringLiteral("全局字体"), view_->fontName);
    auto* fontActions = new QHBoxLayout;
    auto* importFontButton = new C::ActionButton(QStringLiteral("导入 TTF/OTF"), colors);
    auto* systemFontButton = new C::ActionButton(QStringLiteral("恢复系统字体"), colors);
    fontActions->addWidget(importFontButton);
    fontActions->addWidget(systemFontButton);
    colorLayout->addRow(fontActions);
    tabs->addTab(colors, QStringLiteral("颜色与字体"));

    auto* backgrounds = new QWidget(tabs);
    auto* backgroundForm = new QFormLayout(backgrounds);
    view_->backgroundScope = new C::ChoiceField(backgrounds);
    view_->backgroundScope->setObjectName(QStringLiteral("theme.backgroundScope"));
    const QStringList scopes {QStringLiteral("patchbay"), QStringLiteral("mixer"), QStringLiteral("mixer-overview"),
        QStringLiteral("sel-main"), QStringLiteral("sel-aux"), QStringLiteral("sel-fx"),
        QStringLiteral("meter-bridge")};
    for (const auto& scope : scopes) view_->backgroundScope->addItem(scope, scope);
    backgroundForm->addRow(QStringLiteral("模块区域"), view_->backgroundScope);
    view_->imageMode = new C::ChoiceField(backgrounds);
    view_->imageMode->setObjectName(QStringLiteral("theme.imageMode"));
    view_->imageMode->addItem(QStringLiteral("继承接线盘"), QStringLiteral("inherit"));
    view_->imageMode->addItem(QStringLiteral("自定义图片"), QStringLiteral("custom"));
    view_->imageMode->addItem(QStringLiteral("无图片"), QStringLiteral("none"));
    auto* chooseImage = new C::ActionButton(QStringLiteral("选择图片"), backgrounds);
    auto* imageRow = new QHBoxLayout;
    imageRow->addWidget(view_->imageMode, 1);
    imageRow->addWidget(chooseImage);
    backgroundForm->addRow(QStringLiteral("图片来源"), imageRow);
    view_->size = new C::ChoiceField(backgrounds);
    addChoice(view_->size, {QStringLiteral("original"), QStringLiteral("contain"),
                            QStringLiteral("cover"), QStringLiteral("stretch")});
    backgroundForm->addRow(QStringLiteral("尺寸模式"), view_->size);
    view_->scale = new C::DecimalField(backgrounds);
    view_->scale->setRange(0.05, 4.0);
    view_->scale->setSingleStep(0.05);
    view_->scale->setDecimals(2);
    backgroundForm->addRow(QStringLiteral("等比缩放"), view_->scale);
    view_->repeat = new C::ChoiceField(backgrounds);
    addChoice(view_->repeat, {QStringLiteral("no-repeat"), QStringLiteral("repeat"),
                              QStringLiteral("repeat-x"), QStringLiteral("repeat-y")});
    backgroundForm->addRow(QStringLiteral("平铺"), view_->repeat);
    view_->x = new C::TextField(backgrounds);
    view_->y = new C::TextField(backgrounds);
    view_->x->setPlaceholderText(QStringLiteral("50% / 12px"));
    view_->y->setPlaceholderText(QStringLiteral("50% / 12px"));
    backgroundForm->addRow(QStringLiteral("横向位置"), view_->x);
    backgroundForm->addRow(QStringLiteral("纵向位置"), view_->y);
    const auto opacity = [backgrounds] {
        auto* field = new C::DecimalField(backgrounds);
        field->setRange(0.0, 1.0);
        field->setDecimals(2);
        field->setSingleStep(0.05);
        return field;
    };
    view_->imageOpacity = opacity();
    backgroundForm->addRow(QStringLiteral("图片透明度"), view_->imageOpacity);
    view_->blur = new C::DecimalField(backgrounds);
    view_->blur->setObjectName(QStringLiteral("theme.blurRadius"));
    view_->blur->setRange(0.0, 64.0);
    view_->blur->setDecimals(0);
    view_->blur->setSuffix(QStringLiteral(" px"));
    backgroundForm->addRow(QStringLiteral("高斯模糊"), view_->blur);
    view_->componentOpacity = opacity();
    view_->componentOpacity->setObjectName(QStringLiteral("theme.componentOpacity"));
    backgroundForm->addRow(QStringLiteral("组件透明度"), view_->componentOpacity);
    view_->overlay = new C::ToggleField(QStringLiteral("上层覆盖"), backgrounds);
    view_->overlay->setObjectName(QStringLiteral("theme.upperOverlay"));
    backgroundForm->addRow(QStringLiteral("叠层模式"), view_->overlay);
    view_->overlayOpacity = opacity();
    backgroundForm->addRow(QStringLiteral("上层图片透明度"), view_->overlayOpacity);
    view_->overlayHint = new C::TextLabel(QStringLiteral("上层覆盖模式：透明背景图片最佳"), backgrounds);
    view_->overlayHint->setObjectName(QStringLiteral("theme.overlayHint"));
    backgroundForm->addRow(view_->overlayHint);
    auto* applyBackgroundButton = new C::ActionButton(QStringLiteral("应用背景设置"), backgrounds);
    applyBackgroundButton->setObjectName(QStringLiteral("theme.applyBackground"));
    backgroundForm->addRow(applyBackgroundButton);
    tabs->addTab(backgrounds, QStringLiteral("背景"));

    auto* exports = new QWidget(tabs);
    auto* exportForm = new QFormLayout(exports);
    view_->name = new C::TextField(exports);
    view_->author = new C::TextField(exports);
    view_->description = new C::TextField(exports);
    view_->homepageEdit = new C::TextField(exports);
    exportForm->addRow(QStringLiteral("主题名称"), view_->name);
    exportForm->addRow(QStringLiteral("作者"), view_->author);
    exportForm->addRow(QStringLiteral("介绍"), view_->description);
    exportForm->addRow(QStringLiteral("主页"), view_->homepageEdit);
    view_->fragmentScope = new C::ChoiceField(exports);
    QSet<QString> exportScopeSet;
    for (const auto& name : registry.names())
        exportScopeSet.insert(registry.token(name).ownerScope);
    const auto& registeredModules = ThemeNamespaceRegistry::instance();
    for (const auto& moduleId : registeredModules.moduleIds())
        if (const auto descriptor = registeredModules.descriptor(moduleId))
            exportScopeSet.insert(descriptor->namespacePrefix.mid(
                12, descriptor->namespacePrefix.size() - 13));
    QStringList exportScopes = exportScopeSet.values();
    exportScopes.sort();
    for (const auto& scope : exportScopes)
        view_->fragmentScope->addItem(scope, QStringLiteral("--wjn-theme-") + scope + '-');
    exportForm->addRow(QStringLiteral("片段命名空间"), view_->fragmentScope);
    auto* exportFull = new C::ActionButton(QStringLiteral("导出当前完整主题"), exports);
    auto* exportDefault = new C::ActionButton(QStringLiteral("导出默认主题模板"), exports);
    auto* exportFragment = new C::ActionButton(QStringLiteral("导出模块片段"), exports);
    exportForm->addRow(exportFull);
    exportForm->addRow(exportDefault);
    exportForm->addRow(exportFragment);
    tabs->addTab(exports, QStringLiteral("导出"));

    view_->status = new C::TextLabel(this);
    view_->status->setObjectName(QStringLiteral("theme.status"));
    layout->addWidget(view_->status);

    const auto activeMeta = core_.activeFullTheme().metadata();
    view_->name->setText(activeMeta.value("name").toString(QStringLiteral("WinJACKNexus Default")));
    view_->author->setText(activeMeta.value("author").toString(QStringLiteral("WinJACKNexus")));
    view_->description->setText(activeMeta.value("description").toString(
        QStringLiteral("Built-in default appearance")));
    view_->homepageEdit->setText(activeMeta.value("homepage").toString());

    connect(importButton, &QPushButton::clicked, this, [this] {
        QDir().mkpath(themesDirectory());
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("导入主题包"),
            themesDirectory(), QStringLiteral("WinJACKNexus 主题 (*.wjnskin)"));
        QString error;
        if (!path.isEmpty() && !openPackage(path, &error)) view_->status->setText(error);
    });
    connect(view_->preview, &QPushButton::clicked, this, [this] {
        QString error;
        if (!preview(&error)) view_->status->setText(error);
    });
    connect(view_->apply, &QPushButton::clicked, this, [this] {
        QString error;
        if (!apply(&error)) view_->status->setText(error);
    });
    connect(view_->cancel, &QPushButton::clicked, this, [this] { cancelPreview(); });
    connect(view_->homepage, &QPushButton::clicked, this, [this] {
        const auto meta = candidate_.isValid() ? candidate_.metadata() : core_.activeFullTheme().metadata();
        const auto link = meta.value("homepage").toString();
        if (!link.isEmpty()) QDesktopServices::openUrl(QUrl(link));
    });
    connect(view_->colorToken, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const auto name = view_->colorToken->currentData().toString();
        view_->colorName->setText(name);
        view_->colorValue->setText(core_.snapshot()->value(name));
    });
    connect(setColor, &QPushButton::clicked, this, [this] {
        const auto enteredName = view_->colorName->text().trimmed();
        const auto name = enteredName.isEmpty() ? view_->colorToken->currentData().toString() : enteredName;
        const auto value = view_->colorValue->text().trimmed();
        QString error;
        if (!ThemeRegistry::valueMatchesType(QStringLiteral("color"), value)
            || !core_.setOverride(name, value, {}, &error))
            view_->status->setText(error.isEmpty() ? QStringLiteral("颜色值格式错误") : error);
        else view_->status->setText(QStringLiteral("颜色已应用"));
    });
    connect(importFontButton, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择字体"), {},
            QStringLiteral("字体 (*.ttf *.otf)"));
        QString error;
        if (!path.isEmpty() && !importFont(path, &error)) view_->status->setText(error);
    });
    connect(systemFontButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!core_.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                               QStringLiteral("system"), {}, &error)) view_->status->setText(error);
    });
    connect(view_->backgroundScope, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] { refreshBackgroundEditor(); });
    connect(view_->overlay, &QCheckBox::toggled, this, [this](bool enabled) {
        view_->overlayHint->setVisible(enabled);
        view_->blur->setEnabled(!enabled);
        view_->componentOpacity->setEnabled(!enabled);
        view_->overlayOpacity->setEnabled(enabled);
    });
    connect(chooseImage, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择背景图片"), {},
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.webp)"));
        QString error;
        if (!path.isEmpty() && !importImage(path, &error)) view_->status->setText(error);
    });
    connect(applyBackgroundButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!applyBackground(&error)) view_->status->setText(error);
        else view_->status->setText(QStringLiteral("背景设置已应用"));
    });
    const auto exportAction = [this](const QString& scope, bool builtIn) {
        QDir().mkpath(themesDirectory());
        const auto suggested = QDir(themesDirectory()).filePath(builtIn
            ? QStringLiteral("winjacknexus-default.wjnskin")
            : scope.isEmpty() ? QStringLiteral("custom.wjnskin")
                              : QStringLiteral("module-%1.wjnskin").arg(scope.mid(12, scope.size() - 13)));
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出主题"), suggested,
            QStringLiteral("WinJACKNexus 主题 (*.wjnskin)"));
        QString error;
        if (!path.isEmpty() && !exportToFile(path, scope, builtIn, &error)) view_->status->setText(error);
        else if (!path.isEmpty()) view_->status->setText(QStringLiteral("主题包已导出"));
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
}

ThemeManagementDialog::~ThemeManagementDialog() = default;

bool ThemeManagementDialog::openPackage(const QString& path, QString* error) {
    ThemePackage next;
    if (!ThemePackage::loadFromFile(path, &next, error)) return false;
    if (previewing_) cancelPreview();
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

bool ThemeManagementDialog::preview(QString* error) {
    if (!candidate_.isValid() || candidate_.metadata().value("kind") != "full")
        return false;
    if (previewing_) return true;
    if (!core_.beginPreview(candidate_, error)) return false;
    previewing_ = true;
    refresh();
    return true;
}

bool ThemeManagementDialog::apply(QString* error) {
    if (!candidate_.isValid()) return false;
    const bool full = candidate_.metadata().value("kind") == "full";
    if (full) {
        if (!previewing_) {
            ThemeCore probe;
            if (!probe.setFullTheme(candidate_, error)) return false;
        }
        QDir folder(themesDirectory());
        if (!folder.exists() && !QDir().mkpath(folder.absolutePath())) {
            if (error) *error = QStringLiteral("无法创建主题目录");
            return false;
        }
        QString destination = folder.filePath(candidate_.id() + QStringLiteral(".wjnskin"));
        for (const auto& file : folder.entryInfoList(QDir::Files | QDir::NoSymLinks)) {
            if (file.suffix().compare(QStringLiteral("wjnskin"), Qt::CaseInsensitive) != 0) continue;
            ThemePackage existing;
            if (ThemePackage::loadFromFile(file.absoluteFilePath(), &existing)
                && existing.id() == candidate_.id()) {
                destination = file.absoluteFilePath();
                break;
            }
        }
        if (QFileInfo(candidatePath_).absoluteFilePath() != QFileInfo(destination).absoluteFilePath()
            && !candidate_.saveToFile(destination, error)) return false;
    }
    if (previewing_) {
        if (!core_.commitPreview()) return false;
        previewing_ = false;
    } else if (candidate_.metadata().value("kind") == "fragment") {
        if (!core_.applyFragment(candidate_, error)) return false;
    } else if (!core_.setFullTheme(candidate_, error)) return false;
    candidate_ = {};
    candidatePath_.clear();
    refresh();
    return true;
}

bool ThemeManagementDialog::cancelPreview() {
    if (!previewing_) return false;
    previewing_ = false;
    const auto restored = core_.cancelPreview();
    refresh();
    return restored;
}

QJsonObject ThemeManagementDialog::editedMetadata() const {
    return {{"name", view_->name->text().trimmed()},
            {"author", view_->author->text().trimmed()},
            {"description", view_->description->text().trimmed()},
            {"homepage", view_->homepageEdit->text().trimmed()}};
}

bool ThemeManagementDialog::exportToFile(const QString& path, const QString& namespacePrefix,
                                         bool builtInDefault, QString* error) const {
    ThemePackage exported;
    if (builtInDefault) {
        ThemeCore defaults;
        if (!defaults.exportFullTheme({}, &exported, error)) return false;
    } else if (namespacePrefix.isEmpty()) {
        if (!core_.exportFullTheme(editedMetadata(), &exported, error)) return false;
    } else if (!core_.exportFragment(namespacePrefix, editedMetadata(), &exported, error)) {
        return false;
    }
    return exported.saveToFile(path, error);
}

bool ThemeManagementDialog::importImage(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        if (error) *error = QStringLiteral("背景图片超过大小限制或不可读取");
        return false;
    }
    QImageReader reader(&file);
    if (!reader.canRead()) {
        if (error) *error = QStringLiteral("无法读取背景图片");
        return false;
    }
    file.seek(0);
    const auto extension = QFileInfo(path).suffix().toLower();
    const auto logical = QStringLiteral("assets/imported-background.%1").arg(extension);
    const auto scope = view_->backgroundScope->currentData().toString();
    const QJsonObject meta {{"formatVersion", 1}, {"themeApiVersion", 1},
        {"id", "org.winjacknexus-author-image"}, {"name", "Imported image"},
        {"version", "1.0.0"}, {"author", "WinJACKNexus"},
        {"description", "Temporary image resource for theme export"}, {"homepage", ""},
        {"kind", "full"}, {"namespaces", QJsonArray {QStringLiteral("--wjn-theme-") + scope + '-'}}};
    ThemePackage package;
    if (!ThemePackage::create(meta, ":root {}", {{logical, file.readAll()}}, &package, error)) return false;
    importedImages_.insert(scope, package);
    selectValue(view_->imageMode, QStringLiteral("custom"));
    return true;
}

bool ThemeManagementDialog::importFont(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
        if (error) *error = QStringLiteral("字体文件超过大小限制或不可读取");
        return false;
    }
    const auto bytes = file.readAll();
    if (QFontDatabase::addApplicationFontFromData(bytes) < 0) {
        if (error) *error = QStringLiteral("字体文件无效");
        return false;
    }
    const auto logical = QStringLiteral("assets/imported-font.%1")
        .arg(QFileInfo(path).suffix().toLower());
    const QJsonObject meta {{"formatVersion", 1}, {"themeApiVersion", 1},
        {"id", "org.winjacknexus-author-font"}, {"name", "Imported font"},
        {"version", "1.0.0"}, {"author", "WinJACKNexus"},
        {"description", "Temporary font resource for theme export"}, {"homepage", ""},
        {"kind", "full"}, {"namespaces", QJsonArray {"--wjn-theme-global-"}}};
    ThemePackage package;
    if (!ThemePackage::create(meta, ":root {}", {{logical, bytes}}, &package, error)) return false;
    return core_.setOverride(QStringLiteral("--wjn-theme-global-font-source"),
                             QStringLiteral("url(\"%1\")").arg(logical), package, error);
}

bool ThemeManagementDialog::applyBackground(QString* error) {
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
    if (!ThemeRegistry::valueMatchesType(QStringLiteral("position"), x)
        || !ThemeRegistry::valueMatchesType(QStringLiteral("position"), y)) {
        if (error) *error = QStringLiteral("位置须为百分比或 px 值");
        return false;
    }
    if (mode == "none") {
        if (!core_.setOverride(token(scope, QStringLiteral("image")), QStringLiteral("none"), {}, error)) return false;
    } else if (mode == "inherit") {
        if (scope == QStringLiteral("patchbay")) core_.removeOverride(token(scope, QStringLiteral("image")));
        else if (!core_.setOverride(token(scope, QStringLiteral("image")),
                                   QStringLiteral("var(--wjn-theme-patchbay-background-image)"), {}, error)) return false;
    } else {
        const auto imported = importedImages_.value(scope);
        if (imported.isValid()) {
            if (!core_.setOverride(token(scope, QStringLiteral("image")),
                                   QStringLiteral("url(\"%1\")").arg(imported.resourceNames().first()),
                                   imported, error)) return false;
        } else if (!core_.declaredValue(token(scope, QStringLiteral("image"))).startsWith("url(")) {
            if (error) *error = QStringLiteral("先选择一张自定义图片");
            return false;
        }
    }
    const QList<QPair<QString, QString>> values {
        {QStringLiteral("size"), size}, {QStringLiteral("scale"), scale},
        {QStringLiteral("repeat"), repeat}, {QStringLiteral("position-x"), x},
        {QStringLiteral("position-y"), y}, {QStringLiteral("opacity"), imageOpacity},
        {QStringLiteral("blur-radius"), blur}, {QStringLiteral("component-opacity"), componentOpacity},
        {QStringLiteral("overlay-enabled"), overlay ? QStringLiteral("true") : QStringLiteral("false")},
        {QStringLiteral("overlay-opacity"), overlayOpacity}
    };
    for (const auto& [role, value] : values)
        if (!core_.setOverride(token(scope, role), value, {}, error)) return false;
    return true;
}

void ThemeManagementDialog::refreshBackgroundEditor() {
    const auto snapshot = core_.snapshot();
    if (!snapshot) return;
    const auto scope = view_->backgroundScope->currentData().toString();
    const auto value = [&](const QString& role) { return snapshot->value(token(scope, role)); };
    const auto declaredImage = core_.declaredValue(token(scope, QStringLiteral("image")));
    if (declaredImage == QStringLiteral("none") || (scope == QStringLiteral("patchbay") && declaredImage.isEmpty()))
        selectValue(view_->imageMode, QStringLiteral("none"));
    else if (importedImages_.contains(scope) || declaredImage.startsWith("url("))
        selectValue(view_->imageMode, QStringLiteral("custom"));
    else selectValue(view_->imageMode, QStringLiteral("inherit"));
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

void ThemeManagementDialog::refresh() {
    ThemeManager themeAdapter;
    themeAdapter.applyToWidget(this);
    const auto meta = candidate_.isValid() ? candidate_.metadata() : core_.activeFullTheme().metadata();
    const auto name = meta.value("name").toString(QStringLiteral("WinJACKNexus Default"));
    const auto author = meta.value("author").toString(QStringLiteral("WinJACKNexus"));
    const auto description = meta.value("description").toString(QStringLiteral("Built-in default appearance"));
    const auto homepage = meta.value("homepage").toString();
    view_->identity->setText(QStringLiteral("%1\n作者：%2\n%3\n主页：%4")
        .arg(name, author, description, homepage.isEmpty() ? QStringLiteral("未提供") : homepage));
    view_->homepage->setEnabled(!homepage.isEmpty());
    view_->preview->setEnabled(candidate_.isValid() && candidate_.metadata().value("kind") == "full"
                               && !previewing_);
    view_->apply->setEnabled(candidate_.isValid());
    view_->cancel->setEnabled(previewing_);
    QStringList lines;
    for (const auto& item : core_.snapshot()->diagnostics())
        lines.append(QStringLiteral("%1:%2 %3 %4").arg(item.line).arg(item.code, item.token, item.message));
    view_->diagnostics->setPlainText(lines.join('\n'));
    view_->fontName->setText(core_.snapshot()->value(QStringLiteral("--wjn-theme-global-font-source")));
    refreshBackgroundEditor();
}

void ThemeManagementDialog::closeEvent(QCloseEvent* event) {
    if (previewing_) cancelPreview();
    QDialog::closeEvent(event);
}

}
