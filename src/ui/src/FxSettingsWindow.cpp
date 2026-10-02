/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ThemeNamespaceRegistry.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeBackground.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>

#include <QBoxLayout>
#include <QComboBox>
#include <QCoreApplication>
#include <QDirIterator>
#include <QDialog>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QSaveFile>
#include <QUuid>
#include <QFrame>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QLibrary>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace WinJACKNexus::UI {
namespace {
QPushButton* button(const QString& text, QWidget* parent, const char* objectName = "") {
    auto* result = new Controls::ActionButton(text, parent);
    result->setObjectName(QString::fromLatin1(objectName));
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

QFrame* panel(QWidget* parent) {
    auto* result = new QFrame(parent);
    result->setObjectName(QStringLiteral("panel"));
    return result;
}

class FxChainCanvas final : public QWidget {
public:
    explicit FxChainCanvas(QWidget* parent = nullptr) : QWidget(parent) {}

    void setEndpoints(QWidget* input, QWidget* output)
    {
        input_ = input;
        output_ = output;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!input_ || !output_) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF start = input_->geometry().center();
        const QPointF end = output_->geometry().center();
        const qreal y = (start.y() + end.y()) / 2;
        const qreal tip = end.x() - 12;
        painter.setPen(QPen(Controls::themeTokenColor(QStringLiteral("sel-fx"),
            QStringLiteral("chart-chain-connection-stroke"), QColor(90,169,219,220)),
            3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(start.x(), y), QPointF(tip - 13, y));
        painter.setPen(Qt::NoPen);
        painter.setBrush(Controls::themeTokenColor(QStringLiteral("sel-fx"),
            QStringLiteral("chart-chain-arrow-fill"), QColor(90,169,219,230)));
        painter.drawPolygon(QPolygonF {QPointF(tip, y), QPointF(tip - 17, y - 8),
                               QPointF(tip - 17, y + 8)});
    }

private:
    QWidget* input_ {};
    QWidget* output_ {};
};

class FxListDelegate final : public QStyledItemDelegate {
public:
    explicit FxListDelegate(std::function<void(int)> toggle,
                            std::function<void(int, QPoint)> menu, QObject* parent)
        : QStyledItemDelegate(parent), toggle_(std::move(toggle)), menu_(std::move(menu)) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto themed=[](const char* role,const QColor& fallback){
            return Controls::themeTokenColor(QStringLiteral("sel-fx"),QString::fromLatin1(role),fallback);
        };
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF card = option.rect.adjusted(3, 3, -3, -3);
        const bool selected = option.state & QStyle::State_Selected;
        const bool enabled = index.data(Qt::UserRole + 3).toBool();
        const QColor accent(index.data(Qt::UserRole + 2).toString());
        Controls::drawSurface(*painter, card,
                              enabled ? themed("control-fx-chain-card-surface-normal",QColor("#214b62"))
                                      : themed("control-fx-chain-card-surface-disabled",QColor("#15273a")),
                              selected ? themed("control-fx-chain-card-border-selected",QColor("#42cbfb"))
                                       : enabled ? accent
                                                 : themed("control-fx-chain-card-border-disabled",QColor("#344d69")),
                              7, selected ? 2.0 : 1.2);
        const QPointF numberCenter(card.left() + 17, card.center().y());
        Controls::drawMarker(*painter, numberCenter, 12,
                             themed("control-fx-chain-number-surface", QColor("#15273a")),
                             enabled ? themed("control-fx-chain-number-border-normal", QColor("#a8d8fa"))
                                     : themed("control-fx-chain-number-border-disabled", QColor("#667d9a")));
        Controls::drawText(*painter, QRectF(card.left() + 5, card.center().y() - 12, 24, 24),
                           QString::number(index.row() + 1), painter->font(),
                           themed("control-fx-chain-number-text", QColor("#e8f2ff")), Qt::AlignCenter);
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 2; ++column)
                Controls::drawMarker(*painter,
                    QPointF(card.left() + 39 + column * 6, card.top() + 20 + row * 7),
                    1.3, themed("control-fx-chain-grip-marker", QColor("#7998bf")));
        const QRectF icon(card.left() + 58, card.center().y() - 24, 48, 48);
        Controls::drawSurface(*painter, icon,
                              enabled ? accent.darker(140)
                                      : themed("control-fx-chain-icon-surface-disabled", QColor("#273b50")),
                              enabled ? accent.lighter(130) : accent.darker(180), 5);
        QFont iconFont = painter->font(); iconFont.setPixelSize(28); iconFont.setBold(true);
        Controls::drawText(*painter, icon, index.data(Qt::UserRole + 1).toString(),
                           iconFont, enabled ? themed("control-fx-chain-icon-text-normal", QColor(Qt::white))
                                             : themed("control-fx-chain-icon-text-disabled", QColor("#91a6ba")),
                           Qt::AlignCenter);
        QFont nameFont = painter->font(); nameFont.setPixelSize(15); nameFont.setBold(false);
        Controls::drawText(*painter,
                           QRectF(icon.right() + 13, card.top(), card.width() - 175, card.height()),
                           index.data(Qt::DisplayRole).toString(), nameFont,
                           enabled ? themed("control-fx-chain-card-text-normal",QColor("#f1f8ff"))
                                   : themed("control-fx-chain-card-text-disabled",QColor("#93a8be")),
                           Qt::AlignVCenter | Qt::AlignLeft);
        Controls::drawMarker(*painter, QPointF(card.right() - 42, card.center().y()),
                             5, enabled ? themed("control-fx-chain-card-indicator-normal",QColor("#47e5a1"))
                                        : themed("control-fx-chain-card-indicator-disabled",QColor("#52677c")));
        QFont dotsFont = painter->font(); dotsFont.setPixelSize(24);
        Controls::drawText(*painter, QRectF(card.right() - 34, card.top(), 28, card.height()),
                           QStringLiteral("⋮"), dotsFont,
                           themed("control-fx-chain-menu-text", QColor("#a8c6ef")), Qt::AlignCenter);
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() != QEvent::MouseButtonRelease) return false;
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        const QRectF card = option.rect.adjusted(3, 3, -3, -3);
        if (QRectF(card.right() - 34, card.top(), 28, card.height()).contains(mouse->position())) {
            menu_(index.data(Qt::UserRole).toInt(), mouse->globalPosition().toPoint());
            return true;
        }
        if (!card.contains(mouse->position())) return false;
        toggle_(index.data(Qt::UserRole).toInt());
        return true;
    }
private:
    std::function<void(int)> toggle_;
    std::function<void(int, QPoint)> menu_;
};
}

class FxSettingsWindow::Impl {
public:
    using Count = std::uint32_t (*)();
    using Meta = const WjnFxMeta* (*)(std::uint32_t);
    using Create = WjnFxInstance* (*)(std::uint32_t);
    using Clone = WjnFxInstance* (*)(const WjnFxInstance*);
    using Destroy = void (*)(WjnFxInstance*);
    using SelectChannel = void (*)(WjnFxInstance*, int);
    using CreateEmbedded = QWidget* (*)(WjnFxInstance*, QWidget*);
    using SetTheme = void (*)(WjnFxInstance*, const WjnFxTheme*);
    using PresetCount = std::uint32_t (*)();
    using PresetName = const char* (*)(std::uint32_t);
    using CurrentPreset = int (*)(const WjnFxInstance*);
    using SelectPreset = void (*)(WjnFxInstance*, int);
    using SnapshotSize = std::uint32_t (*)();
    using ExportSnapshot = int (*)(const WjnFxInstance*, void*, std::uint32_t);
    using ImportSnapshot = int (*)(WjnFxInstance*, const void*, std::uint32_t);
    using SetChanged = void (*)(WjnFxInstance*, WjnFxParametersChanged, void*);
    using SetLive = void (*)(WjnFxInstance*, WjnFxLiveAnalysisReader, void*);

    struct AnalysisContext { Impl* owner {}; int id {}; };

    struct Module {
        QLibrary library;
        ThemeNamespaceRegistry::Lease themeLease;
        WjnFxMeta info {};
        Create create {};
        Clone clone {};
        Destroy destroy {};
        SelectChannel selectChannel {};
        CreateEmbedded createEditor {};
        SetTheme setTheme {};
        PresetCount presetCount {};
        PresetName presetName {};
        CurrentPreset currentPreset {};
        SelectPreset selectPreset {};
        SnapshotSize snapshotSize {};
        ExportSnapshot exportSnapshot {};
        ImportSnapshot importSnapshot {};
        SetChanged setChanged {};
        SetLive setLive {};
    };

    struct Effect {
        int id {};
        Module* module {};
        QString name, glyph, color;
        WjnFxInstance* instance {};
        QWidget* editor {};
        bool enabled = false;
        std::shared_ptr<AnalysisContext> analysisContext;
    };

    static QColor color(const char* role, const char* fallback) {
        return Controls::themeTokenColor(QStringLiteral("sel-fx"), QString::fromLatin1(role),
                                         QColor(QString::fromLatin1(fallback)));
    }

    static QString cssColor(const char* role, const char* fallback) {
        const auto value = color(role, fallback);
        if (value.alpha() == 255) return value.name();
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(value.red()).arg(value.green()).arg(value.blue()).arg(value.alphaF());
    }

    void applyLegacyModuleTheme(Module& module, WjnFxInstance* instance) {
        if (!module.setTheme || !instance) return;
        static const WjnFxTheme theme {"#0b1929", "#14283e", "#e9f2ff", "#31c6ff", "#29435d"};
        module.setTheme(instance, &theme);
    }

    void applyTheme() {
        QString style = QStringLiteral(R"(
            #fxWindow { background:%surface-window%; color:%text-primary%; font-size:15px; }
            QFrame#panel { background:%surface-panel%; border:1px solid %border-normal%; border-radius:9px; }
            QLabel { color:%text-label%; background:transparent; }
            QPushButton, QToolButton, QComboBox { color:%button-text-normal%; background:%button-surface-normal%; border:1px solid %button-border-normal%; border-radius:6px; padding:7px 13px; }
            QPushButton:hover, QToolButton:hover { border-color:%button-border-hover%; background:%button-surface-hover%; }
            QPushButton#primary, QPushButton:checked { border-color:%accent-primary%; background:%button-surface-selected%; color:%button-text-selected%; }
            QListWidget { background:transparent; border:0; outline:0; }
            QListWidget::item { background:%list-surface-normal%; border:1px solid %list-border-normal%; border-radius:7px; margin:3px 2px; color:%list-text-normal%; }
            QListWidget::item:selected { border:1px solid %list-border-selected%; background:%list-surface-selected%; }
            QScrollArea { background:%scroll-surface-normal%; border:1px solid %border-normal%; border-radius:7px; }
            QScrollArea#fxChainScroll { background:transparent; border:0; }
            QWidget#fxChainContent { background:transparent; }
            QLabel[fxChainEndpoint="true"] { background:%endpoint-surface%; border:1px solid %endpoint-border%; border-radius:7px; color:%endpoint-text%; font-size:16px; font-weight:bold; }
            QScrollBar { background:%scrollbar-surface-normal%; }
            QPushButton[fxChainPresetAction="true"] { color:%preset-text%; background:%preset-surface%; border-color:%preset-border%; }
            QPushButton[fxChainPresetAction="true"]:hover { background:%preset-hover-surface%; border-color:%preset-hover-border%; }
        )");
        const std::array<std::array<const char*, 3>, 28> replacements {{
            {"%preset-text%", "control-preset-button-text", "#d8e8ff"},
            {"%preset-surface%", "control-preset-button-surface", "#192e45"},
            {"%preset-border%", "control-preset-button-border", "#405e7f"},
            {"%preset-hover-surface%", "control-preset-button-hover-surface", "#214263"},
            {"%preset-hover-border%", "control-preset-button-hover-border", "#37b9fa"},
            {"%surface-window%", "surface-window", "#0b1929"},
            {"%surface-panel%", "surface-panel", "#14283e"},
            {"%text-primary%", "text-primary", "#e9f2ff"},
            {"%text-label%", "text-label", "#e5efff"},
            {"%border-normal%", "border-normal", "#29435d"},
            {"%accent-primary%", "accent-primary", "#31c6ff"},
            {"%button-text-normal%", "control-button-text-normal", "#d8e8ff"},
            {"%button-surface-normal%", "control-button-surface-normal", "#192e45"},
            {"%button-border-normal%", "control-button-border-normal", "#405e7f"},
            {"%button-border-hover%", "control-button-border-hover", "#37b9fa"},
            {"%button-surface-hover%", "control-button-surface-hover", "#214263"},
            {"%button-surface-selected%", "control-button-surface-selected", "#153d60"},
            {"%button-text-selected%", "control-button-text-selected", "#89e7ff"},
            {"%list-surface-normal%", "control-list-surface-normal", "#172c42"},
            {"%list-border-normal%", "control-list-border-normal", "#304b69"},
            {"%list-text-normal%", "control-list-text-normal", "#f0f5ff"},
            {"%list-border-selected%", "control-list-border-selected", "#38c9fc"},
            {"%list-surface-selected%", "control-list-surface-selected", "#173d54"},
            {"%scroll-surface-normal%", "control-scroll-surface-normal", "#10263a"},
            {"%scrollbar-surface-normal%", "control-scrollbar-surface-normal", "#12263b"},
            {"%endpoint-surface%", "control-chain-endpoint-surface", "#243e57"},
            {"%endpoint-border%", "control-chain-endpoint-border", "#567a9a"},
            {"%endpoint-text%", "control-chain-endpoint-text", "#f1f7ff"}
        }};
        for (const auto& entry : replacements)
            style.replace(QString::fromLatin1(entry[0]), cssColor(entry[1], entry[2]));
        owner.setStyleSheet(style);
        if (chainContent) chainContent->update();
        if (list) list->viewport()->update();
        owner.update();
    }

    explicit Impl(FxSettingsWindow& window, const QString& source, const QStringList& sourceChannels)
        : owner(window), sourceChannels(sourceChannels) {
        owner.setWindowFlag(Qt::Window);
        owner.setWindowTitle(QStringLiteral("FX 设置 - %1").arg(source));
        owner.resize(1398, 874);
        owner.setMinimumSize(1100, 720);
        owner.setObjectName(QStringLiteral("fxWindow"));
        applyTheme();

        auto* root = new QVBoxLayout(&owner);
        root->setContentsMargins(6, 5, 6, 8);
        root->setSpacing(12);
        auto* header = panel(&owner);
        header->setFixedHeight(64);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(20, 5, 20, 5);
        auto* leftControls = new QWidget(header);
        leftControls->setObjectName(QStringLiteral("fxHeaderLeft"));
        leftControls->setStyleSheet(QStringLiteral("QWidget#fxHeaderLeft { background: transparent; border: 0; }"));
        auto* leftLayout = new QHBoxLayout(leftControls);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(12);
        auto* title = new Controls::TextLabel(owner.windowTitle(), leftControls);
        title->setObjectName(QStringLiteral("fxSourceTitle"));
        QFont titleFont = title->font(); titleFont.setPixelSize(24); titleFont.setBold(true); title->setFont(titleFont);
        leftLayout->addWidget(title);
        QFont bold = title->font(); bold.setPixelSize(19);
        master = new Controls::ToggleButton(leftControls);
        master->setObjectName(QStringLiteral("fxChainToggle"));
        master->setChecked(false);
        master->setToolTip(QStringLiteral("FX 链总开关"));
        QObject::connect(master, &QAbstractButton::toggled, &owner, [this](bool enabled) {
            syncChainPower();
            if (!loading && chainEnabledHandler) chainEnabledHandler(enabled);
            publish();
        });
        leftLayout->addWidget(master);
        leftLayout->addStretch();
        auto* channelControls = new QWidget(header);
        channelControls->setObjectName(QStringLiteral("fxChannelControls"));
        channelControls->setStyleSheet(QStringLiteral("QWidget#fxChannelControls { background: transparent; border: 0; }"));
        auto* channelLayout = new QHBoxLayout(channelControls);
        channelLayout->setContentsMargins(0, 0, 0, 0);
        channelLayout->setSpacing(8);
        channelLayout->addWidget(new Controls::TextLabel(QStringLiteral("作用声道："), channelControls));
        channelRow = new QHBoxLayout;
        channelRow->setSpacing(6);
        QStringList visibleChannels { QStringLiteral("All") };
        if(sourceChannels.size()>1)visibleChannels.append(sourceChannels);
        const auto nativeChannels = WinJACKNexus::Common::standardChannelLabels(8);
        for (const auto& name : visibleChannels) {
            auto* choice = button(name, channelControls);
            choice->setObjectName(QStringLiteral("fxChannel%1").arg(name));
            choice->setCheckable(true);
            choice->setMinimumWidth(54);
            choice->setChecked(name == QStringLiteral("All"));
            channelButtons.push_back(choice);
            const int channelIndex = name == QStringLiteral("All") ? -1 : nativeChannels.indexOf(name);
            QObject::connect(choice, &QPushButton::clicked, &owner, [this, choice, channelIndex] {
                chooseChannel(choice,channelIndex);
            });
            channelRow->addWidget(choice);
        }
        channelLayout->addLayout(channelRow);
        auto* rightControls = new QWidget(header);
        rightControls->setObjectName(QStringLiteral("fxHeaderRight"));
        rightControls->setStyleSheet(QStringLiteral("QWidget#fxHeaderRight { background: transparent; border: 0; }"));
        auto* rightLayout = new QHBoxLayout(rightControls);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(8);
        rightLayout->addStretch();
        rightLayout->addWidget(new Controls::TextLabel(QStringLiteral("预设："), rightControls));
        presets = new Controls::ChoiceField(rightControls);
        presets->setObjectName(QStringLiteral("fxWindowPreset"));
        presets->addItem(QStringLiteral("当前效果链"));
        presets->setMinimumWidth(185);
        rightLayout->addWidget(presets);
        auto* save = button(QStringLiteral("保存"), rightControls, "fxChainPresetSave");
        auto* saveAs = button(QStringLiteral("另存为"), rightControls, "fxChainPresetSaveAs");
        auto* refresh = button(QStringLiteral("刷新"), rightControls, "fxChainPresetRefresh");
        for (auto* action : {save, saveAs, refresh}) {
            action->setProperty("fxChainPresetAction", true);
            rightLayout->addWidget(action);
        }
        QObject::connect(save, &QPushButton::clicked, &owner, [this] { savePreset(false); });
        QObject::connect(saveAs, &QPushButton::clicked, &owner, [this] { savePreset(true); });
        QObject::connect(refresh, &QPushButton::clicked, &owner, [this] { refreshPresets(); });
        QObject::connect(presets, &QComboBox::activated, &owner, [this](int index) { selectChainPreset(index); });
        const auto sideWidth = std::max(leftControls->sizeHint().width(), rightControls->sizeHint().width());
        leftControls->setMinimumWidth(sideWidth);
        rightControls->setMinimumWidth(sideWidth);
        headerLayout->addWidget(leftControls, 1);
        headerLayout->addWidget(channelControls, 0, Qt::AlignCenter);
        headerLayout->addWidget(rightControls, 1);
        root->addWidget(header);

        auto* body = new QVBoxLayout;
        body->setSpacing(12);
        root->addLayout(body, 1);
        auto* modulePanel = panel(&owner);
        auto* moduleLayout = new QVBoxLayout(modulePanel);
        moduleLayout->setContentsMargins(12, 10, 12, 10);
        auto* moduleHeader = new QHBoxLayout;
        modulePower = button(QStringLiteral("⏻"), modulePanel);
        modulePower->setObjectName(QStringLiteral("fxModulePower"));
        modulePower->setCheckable(true);
        modulePower->setFixedWidth(40);
        moduleHeader->addWidget(modulePower);
        moduleName = new Controls::TextLabel(QStringLiteral("模块设置"), modulePanel);
        moduleName->setObjectName(QStringLiteral("fxModuleTitle"));
        moduleName->setFont(bold);
        moduleHeader->addWidget(moduleName);
        moduleHeader->addStretch();
        moduleHeader->addWidget(new Controls::TextLabel(QStringLiteral("预设："), modulePanel));
        modulePreset = new Controls::ChoiceField(modulePanel);
        modulePreset->setObjectName(QStringLiteral("fxModulePreset"));
        modulePreset->setMinimumWidth(170);
        moduleHeader->addWidget(modulePreset);
        auto* previous = button(QStringLiteral("〈"), modulePanel);
        auto* next = button(QStringLiteral("〉"), modulePanel);
        moduleHeader->addWidget(previous);
        moduleHeader->addWidget(next);
        moduleLayout->addLayout(moduleHeader);
        editorArea = new Controls::ScrollField(modulePanel);
        editorArea->setObjectName(QStringLiteral("fxEditorArea"));
        editorArea->setWidgetResizable(false);
        editorArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        editorParking = new QWidget(&owner);
        editorParking->hide();
        moduleLayout->addWidget(editorArea, 1);
        body->addWidget(modulePanel, 1);

        auto* bottom = new QHBoxLayout;
        bottom->setSpacing(12);
        auto* footer = panel(&owner);
        footer->setFixedHeight(154);
        auto* footerLayout = new QVBoxLayout(footer);
        footerLayout->setContentsMargins(13, 8, 13, 7);
        auto* chainHeader = new QHBoxLayout;
        auto* chainLabel = new Controls::TextLabel(QStringLiteral("效果链"), footer);
        chainLabel->setFont(bold);
        chainHeader->addWidget(chainLabel);
        chainHeader->addStretch();
        auto* add = button(QStringLiteral("＋  添加模块"), footer, "primary");
        chainHeader->addWidget(add);
        footerLayout->addLayout(chainHeader);
        chainScroll = new Controls::ScrollField(footer);
        chainScroll->setObjectName(QStringLiteral("fxChainScroll"));
        chainScroll->setFrameShape(QFrame::NoFrame);
        chainScroll->setWidgetResizable(true);
        chainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        chainScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        chainScroll->viewport()->setStyleSheet(QStringLiteral("background:transparent;"));
        chainContent = new FxChainCanvas(chainScroll);
        chainContent->setObjectName(QStringLiteral("fxChainContent"));
        chainContent->setMinimumHeight(82);
        auto* flowLayout = new QHBoxLayout(chainContent);
        flowLayout->setContentsMargins(4, 0, 4, 0);
        flowLayout->setSpacing(22);
        flowLayout->setSizeConstraint(QLayout::SetMinimumSize);
        const auto endpoint = [this](const QString& symbol, const QString& caption) {
            auto* chip = new Controls::TextLabel(QStringLiteral("%1\n%2").arg(symbol, caption),chainContent);
            chip->setObjectName(QStringLiteral("fxChain%1").arg(symbol));
            chip->setProperty("fxChainEndpoint", true);
            chip->setFixedSize(72,62);
            chip->setAlignment(Qt::AlignCenter);
            return chip;
        };
        flowLayout->addStretch();
        auto* inputChip=endpoint(QStringLiteral("In"),QStringLiteral("输入"));
        flowLayout->addWidget(inputChip);
        list = new Controls::ListField(chainContent);
        list->setObjectName(QStringLiteral("fxChainList"));
        list->setFrameShape(QFrame::NoFrame);
        list->setViewMode(QListView::ListMode);
        list->setFlow(QListView::LeftToRight);
        list->setWrapping(false);
        list->setResizeMode(QListView::Adjust);
        list->setSpacing(3);
        list->setFixedSize(1,78);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setDragEnabled(true);
        list->setAcceptDrops(true);
        list->setDropIndicatorShown(true);
        list->setItemDelegate(new FxListDelegate([this](int id) {
            if (!master->isChecked()) return;
            for (auto& effect : effects) if (effect.id == id) {
                effect.enabled = !effect.enabled;
                for (int row = 0; row < list->count(); ++row)
                    if (list->item(row)->data(Qt::UserRole).toInt() == id)
                        refreshListItem(list->item(row));
                if (auto* current = selected(); current && current->id == id) {
                    const QSignalBlocker blocker(modulePower);
                    modulePower->setChecked(effect.enabled);
                }
                publish();
                break;
            }
        }, [this](int id, QPoint position) { showEffectMenu(id, position); }, list));
        flowLayout->addWidget(list);
        auto* outputChip=endpoint(QStringLiteral("Out"),QStringLiteral("输出"));
        flowLayout->addWidget(outputChip);
        flowLayout->addStretch();
        chainContent->setEndpoints(inputChip,outputChip);
        chainScroll->setWidget(chainContent);
        footerLayout->addWidget(chainScroll,1);
        bottom->addWidget(footer, 1);
        auto* meterPanel = panel(&owner);
        meterPanel->setFixedWidth(290);
        auto* meterLayout = new QVBoxLayout(meterPanel);
        meterLayout->setContentsMargins(12, 7, 12, 7);
        auto* meterSelectors = new QHBoxLayout;
        meterTitle = new Controls::TextLabel(QStringLiteral("输入/输出电平"), meterPanel);
        meterTitle->setObjectName(QStringLiteral("fxMeterTitle"));
        meterSelectors->addWidget(meterTitle);
        meterSelectors->addStretch();
        meterLayout->addLayout(meterSelectors);
        auto* meter = new FxLevelMeterWidget(meterPanel);
        meterLayout->addWidget(meter);
        auto updateMeters = [this, meter, nativeChannels] {
            const QString selected = selectedChannelLabel;
            const auto channelIndex = [&nativeChannels](const QString& channel) {
                return channel == QStringLiteral("M") ? 0 : nativeChannels.indexOf(channel);
            };
            const auto level = [&](const std::array<double, 8>& values) {
                if (selected != QStringLiteral("All")) {
                    const int index = channelIndex(selected);
                    return index >= 0 ? values[static_cast<std::size_t>(index)] : -72.0;
                }
                double power = 0.0;
                for (const auto& channel : this->sourceChannels) {
                    const int index = channelIndex(channel);
                    if (index >= 0) power += std::pow(10.0, values[static_cast<std::size_t>(index)] / 10.0);
                }
                return power > 0.0 ? 10.0 * std::log10(power / std::max(1.0, static_cast<double>(this->sourceChannels.size()))) : -72.0;
            };
            meter->setLevels(level(simulatedInput), level(simulatedOutput));
        };
        auto* meterTimer = new QTimer(meterPanel);
        bindRefreshTimer(*meterTimer, meterPanel);
        QObject::connect(meterTimer, &QTimer::timeout, &owner, [this, updateMeters] {
            if (meterRead) {
                for (std::size_t channel = 0; channel < simulatedInput.size(); ++channel) {
                    simulatedInput[channel] = channelMeterRead
                        ? channelMeterRead(false, channel) : meterRead(false);
                    simulatedOutput[channel] = channelMeterRead
                        ? channelMeterRead(true, channel) : meterRead(true);
                }
                updateMeters();
                syncModulePreset();
                return;
            }
            for (std::size_t index = 0; index < simulatedInput.size(); ++index) {
                const double target = -31.0 + QRandomGenerator::global()->bounded(28.0);
                simulatedInput[index] = std::clamp(simulatedInput[index] * .72 + target * .28, -72.0, 12.0);
                simulatedOutput[index] = std::clamp(simulatedInput[index] - 2.0, -72.0, 12.0);
            }
            updateMeters();
            syncModulePreset();
        });
        meterRefresh=updateMeters;
        meterTimer->start();
        updateMeters();
        bottom->addWidget(meterPanel);
        body->addLayout(bottom);

        QObject::connect(add, &QPushButton::clicked, &owner, [this, add] { showAddMenu(add); });
        QObject::connect(list, &QListWidget::currentItemChanged, &owner, [this] { showSelected(); });
        QObject::connect(list->model(), &QAbstractItemModel::rowsMoved, &owner, [this] { refreshFlow(); publish(); });
        QObject::connect(modulePower, &QPushButton::toggled, &owner, [this](bool enabled) {
            if (!master->isChecked()) return;
            if (auto* effect = selected()) { effect->enabled = enabled; refreshListItem(list->currentItem()); publish(); }
        });
        QObject::connect(modulePreset, &QComboBox::currentIndexChanged, &owner, [this](int index) {
            if (auto* effect = selected(); effect && effect->module->selectPreset && index >= 0) {
                effect->module->selectPreset(effect->instance, index);
                syncModulePreset();
                publish();
            }
        });
        QObject::connect(previous, &QPushButton::clicked, &owner, [this] { stepModulePreset(-1); });
        QObject::connect(next, &QPushButton::clicked, &owner, [this] { stepModulePreset(1); });

        const QString modulesPath = QCoreApplication::applicationDirPath() + QStringLiteral("/nodes/native_fx");
        QDirIterator modules(modulesPath, {QStringLiteral("*.dll")}, QDir::Files, QDirIterator::Subdirectories);
        while (modules.hasNext()) {
            auto candidate = std::make_unique<Module>();
            candidate->library.setFileName(modules.next());
            if (!candidate->library.load()) continue;
            const auto version = reinterpret_cast<Count>(candidate->library.resolve("wjn_fx_api_version"));
            const auto count = reinterpret_cast<Count>(candidate->library.resolve("wjn_fx_count"));
            const auto meta = reinterpret_cast<Meta>(candidate->library.resolve("wjn_fx_meta"));
            candidate->create = reinterpret_cast<Create>(candidate->library.resolve("wjn_fx_create_mixer"));
            candidate->clone = reinterpret_cast<Clone>(candidate->library.resolve("wjn_fx_clone_mixer"));
            candidate->destroy = reinterpret_cast<Destroy>(candidate->library.resolve("wjn_fx_destroy_mixer"));
            candidate->selectChannel = reinterpret_cast<SelectChannel>(candidate->library.resolve("wjn_fx_set_channel"));
            candidate->createEditor = reinterpret_cast<CreateEmbedded>(candidate->library.resolve("wjn_fx_create_mixer_editor"));
            candidate->setTheme = reinterpret_cast<SetTheme>(candidate->library.resolve("wjn_fx_set_theme"));
            candidate->presetCount = reinterpret_cast<PresetCount>(candidate->library.resolve("wjn_fx_preset_count"));
            candidate->presetName = reinterpret_cast<PresetName>(candidate->library.resolve("wjn_fx_preset_name"));
            candidate->currentPreset = reinterpret_cast<CurrentPreset>(candidate->library.resolve("wjn_fx_current_preset"));
            candidate->selectPreset = reinterpret_cast<SelectPreset>(candidate->library.resolve("wjn_fx_select_preset"));
            candidate->snapshotSize = reinterpret_cast<SnapshotSize>(candidate->library.resolve("wjn_fx_parameter_snapshot_size"));
            candidate->exportSnapshot = reinterpret_cast<ExportSnapshot>(candidate->library.resolve("wjn_fx_export_parameter_snapshot"));
            candidate->importSnapshot = reinterpret_cast<ImportSnapshot>(candidate->library.resolve("wjn_fx_import_parameter_snapshot"));
            candidate->setChanged = reinterpret_cast<SetChanged>(candidate->library.resolve("wjn_fx_set_parameter_change_callback"));
            candidate->setLive = reinterpret_cast<SetLive>(candidate->library.resolve("wjn_fx_set_live_analysis_reader"));
            if (!version || version() != WJN_FX_API_VERSION || !count || count() != 1 || !meta
                || !candidate->create || !candidate->destroy || !candidate->selectChannel || !candidate->createEditor
                || !candidate->snapshotSize || !candidate->exportSnapshot || !candidate->importSnapshot || !candidate->setChanged)
                continue;
            const auto* info = meta(0);
            if (!info || !(info->placements & WJN_FX_MIXER)) continue;
            QString themeError;
            ThemeNamespaceRegistry::instance().registerNativeFxModule(
                [&library = candidate->library](const char* name) { return library.resolve(name); },
                candidate->library.fileName(), &candidate->themeLease, &themeError);
            if (!themeError.isEmpty()) qWarning().noquote() << "Native Fx theme descriptor:" << themeError;
            candidate->info = *info;
            available.push_back(candidate.get());
            loadedModules.push_back(std::move(candidate));
        }
        std::sort(available.begin(), available.end(), [](const Module* a, const Module* b) {
            static const QStringList order {"gate", "graphic-eq", "deesser", "compressor",
                "limiter", "delay", "parametric-eq", "reverb"};
            return order.indexOf(QString::fromUtf8(a->info.id)) < order.indexOf(QString::fromUtf8(b->info.id));
        });
        for (auto* module : available) {
            if (QString::fromUtf8(module->info.id) == QStringLiteral("parametric-eq")) {
                addEffect(module, false);
                break;
            }
        }
        if (available.isEmpty()) {
            moduleName->setText(QStringLiteral("Native Fx 加载失败"));
            editorArea->setWidget(new Controls::TextLabel(QStringLiteral("找不到 Native Fx 模块：%1").arg(modulesPath)));
        }
        refreshPresets();
        if (auto* core = ThemeManager::installedThemeCore())
            QObject::connect(core, &ThemeCore::themeChanged, &owner, [this] { applyTheme(); });
    }

    ~Impl() {
        // Each instance owns its editor; remove it from the viewport before destruction.
        editorArea->takeWidget();
        for (const auto& effect : effects) if (effect.instance) effect.module->destroy(effect.instance);
    }

    void chooseChannel(QPushButton* choice, int channelIndex)
    {
        for (auto* other : channelButtons) other->setChecked(other == choice);
        selectedChannel = channelIndex;
        selectedChannelLabel = choice->text();
        for (auto& effect : effects)
            if (effect.instance) effect.module->selectChannel(effect.instance, selectedChannel);
        if(meterTitle)meterTitle->setText(selectedChannel<0?QStringLiteral("输入/输出电平")
            :QStringLiteral("输入/输出(%1)").arg(selectedChannelLabel));
        if(meterRefresh)meterRefresh();
        if(auto* effect=selected())
            moduleName->setText(QStringLiteral("模块设置 - %1%2").arg(effect->name,
                selectedChannel<0?QString():QStringLiteral(" · %1").arg(selectedChannelLabel)));
    }

    void setSourceChannels(const QStringList& channels) {
        if (sourceChannels == channels) return;
        sourceChannels = channels;
        while (auto* item = channelRow->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        channelButtons.clear();
        QStringList visibleChannels { QStringLiteral("All") };
        if(channels.size()>1)visibleChannels.append(channels);
        const auto nativeChannels = WinJACKNexus::Common::standardChannelLabels(8);
        for (const auto& name : visibleChannels) {
            auto* choice = button(name, channelRow->parentWidget());
            choice->setObjectName(QStringLiteral("fxChannel%1").arg(name));
            choice->setCheckable(true);
            choice->setMinimumWidth(61);
            choice->setChecked(name == QStringLiteral("All"));
            channelButtons.push_back(choice);
            const int channelIndex = name == QStringLiteral("All") ? -1 : nativeChannels.indexOf(name);
            QObject::connect(choice, &QPushButton::clicked, &owner, [this, choice, channelIndex] {
                chooseChannel(choice,channelIndex);
            });
            channelRow->addWidget(choice);
        }
        selectedChannel = -1;
        selectedChannelLabel = QStringLiteral("All");
        if(meterTitle)meterTitle->setText(QStringLiteral("输入/输出电平"));
        if(meterRefresh)meterRefresh();
        for (auto& effect : effects)
            if (effect.instance) effect.module->selectChannel(effect.instance, selectedChannel);
    }

    Effect* selected() {
        auto* item = list->currentItem();
        if (!item) return nullptr;
        const int id = item->data(Qt::UserRole).toInt();
        for (auto& effect : effects) if (effect.id == id) return &effect;
        return nullptr;
    }

    void refreshListItem(QListWidgetItem* item) {
        if (!item) return;
        const int id = item->data(Qt::UserRole).toInt();
        for (const auto& effect : effects) if (effect.id == id) {
            item->setText(effect.name);
            item->setSizeHint(QSize(cardWidth(effect.name),74));
            item->setData(Qt::UserRole + 1, effect.glyph);
            item->setData(Qt::UserRole + 2, effect.color);
            item->setData(Qt::UserRole + 3, master->isChecked() && effect.enabled);
            return;
        }
    }

    void updateChainListWidth() {
        int width=4;
        for(int row=0;row<list->count();++row)
            width+=list->item(row)->sizeHint().width()+list->spacing();
        list->setFixedWidth(std::max(1,width));
        chainContent->adjustSize();
        chainContent->updateGeometry();
    }

    int cardWidth(const QString& name) const
    {
        QFont font=list->font();
        font.setPixelSize(15);
        return std::clamp(195+QFontMetrics(font).horizontalAdvance(name),230,420);
    }

    void syncChainPower() {
        for (int row = 0; row < list->count(); ++row) refreshListItem(list->item(row));
        const QSignalBlocker blocker(modulePower);
        modulePower->setChecked(master->isChecked() && selected() && selected()->enabled);
        modulePower->setEnabled(master->isChecked() && selected());
    }

    int rowForEffect(int id) const {
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toInt() == id) return row;
        return -1;
    }

    static int readLive(void* context, float input[8], float output[8],
                        float reduction[8], void* spectrum, std::uint32_t bytes) {
        const auto* source = static_cast<AnalysisContext*>(context);
        if (!source || !source->owner || !source->owner->liveAnalysisRead) return 0;
        const int row = source->owner->rowForEffect(source->id);
        return row >= 0 && source->owner->liveAnalysisRead(
            static_cast<std::size_t>(row), input, output, reduction, spectrum, bytes) ? 1 : 0;
    }

    Effect* effectFor(int id) {
        for (auto& effect : effects) if (effect.id == id) return &effect;
        return nullptr;
    }

    void insertEffect(Module* module, WjnFxInstance* instance, bool enabled, int row) {
        if (!instance) return;
        const auto* info = &module->info;
        Effect effect;
        effect.id = nextId++;
        effect.module = module;
        effect.name = QString::fromUtf8(info->name);
        effect.glyph = QString::fromUtf8(info->iconGlyph);
        effect.color = QString::fromLatin1(info->iconColor);
        effect.instance = instance;
        effect.analysisContext = std::make_shared<AnalysisContext>(AnalysisContext {this, effect.id});
        module->setChanged(instance, [](void* context) { static_cast<Impl*>(context)->publish(); }, this);
        if (module->setLive) module->setLive(instance, &Impl::readLive, effect.analysisContext.get());
        applyLegacyModuleTheme(*module, instance);
        effect.enabled = enabled;
        effects.push_back(effect);
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, effect.id);
        item->setSizeHint(QSize(cardWidth(effect.name),74));
        list->insertItem(row, item);
        refreshListItem(item);
        updateChainListWidth();
        list->setCurrentItem(item);
        refreshFlow();
        publish();
    }

    void addEffect(Module* module, bool enabled) {
        insertEffect(module, module->create(0), enabled, list->count());
    }

    void cloneEffect(int id) {
        auto* effect = effectFor(id);
        const int row = rowForEffect(id);
        if (!effect || !effect->module->clone || row < 0) return;
        auto* instance = effect->module->clone(effect->instance);
        insertEffect(effect->module, instance, effect->enabled, row + 1);
    }

    void cloneChainFrom(const Impl& source) {
        const bool wasLoading = loading;
        loading = true;
        while (!effects.isEmpty()) deleteEffect(effects.back().id);
        missingEffects = source.missingEffects;
        for (int row = 0; row < source.list->count(); ++row) {
            const int id = source.list->item(row)->data(Qt::UserRole).toInt();
            const auto found = std::find_if(source.effects.cbegin(), source.effects.cend(), [id](const Effect& value) {
                return value.id == id;
            });
            if (found == source.effects.cend() || !found->module->clone) continue;
            const auto module = std::find_if(available.cbegin(), available.cend(), [found](const Module* value) {
                return QString::fromUtf8(value->info.id) == QString::fromUtf8(found->module->info.id);
            });
            if (module != available.cend())
                insertEffect(*module, found->module->clone(found->instance), found->enabled, list->count());
        }
        loading = wasLoading;
        publish();
    }

    void deleteEffect(int id) {
        const int row = rowForEffect(id);
        if (row < 0) return;
        for (int index = 0; index < effects.size(); ++index) {
            if (effects[index].id != id) continue;
            if (editorArea->widget() == effects[index].editor) editorArea->takeWidget();
            effects[index].module->destroy(effects[index].instance);
            effects.removeAt(index);
            break;
        }
        {
            const QSignalBlocker blocker(list);
            delete list->takeItem(row);
            list->setCurrentRow(qMin(row, list->count() - 1));
        }
        updateChainListWidth();
        showSelected();
        refreshFlow();
        publish();
    }

    void replaceEffect(int id, Module* module) {
        auto* effect = effectFor(id);
        if (!effect || !module) return;
        auto* instance = module->create(0);
        if (!instance) return;
        if (editorArea->widget() == effect->editor) editorArea->takeWidget();
        effect->module->destroy(effect->instance);
        effect->module = module;
        effect->instance = instance;
        module->setChanged(instance, [](void* context) { static_cast<Impl*>(context)->publish(); }, this);
        if (module->setLive) module->setLive(instance, &Impl::readLive, effect->analysisContext.get());
        effect->editor = nullptr;
        effect->name = QString::fromUtf8(module->info.name);
        effect->glyph = QString::fromUtf8(module->info.iconGlyph);
        effect->color = QString::fromLatin1(module->info.iconColor);
        applyLegacyModuleTheme(*module, instance);
        refreshListItem(list->item(rowForEffect(id)));
        updateChainListWidth();
        if (selected() && selected()->id == id) showSelected();
        refreshFlow();
        publish();
    }

    void showEffectMenu(int id, QPoint position) {
        auto* effect = effectFor(id);
        if (!effect) return;
        QMenu menu(list);
        auto* clone = menu.addAction(QStringLiteral("克隆"));
        clone->setEnabled(effect->module->clone != nullptr);
        QObject::connect(clone, &QAction::triggered, &owner, [this, id] { cloneEffect(id); });
        auto* remove = menu.addAction(QStringLiteral("删除"));
        QObject::connect(remove, &QAction::triggered, &owner, [this, id] { deleteEffect(id); });
        auto* replacements = menu.addMenu(QStringLiteral("替换"));
        for (auto* module : available) {
            const auto* info = &module->info;
            auto* action = replacements->addAction(QStringLiteral("%1   %2").arg(
                QString::fromUtf8(info->iconGlyph), QString::fromUtf8(info->name)));
            QObject::connect(action, &QAction::triggered, &owner,
                             [this, id, module] { replaceEffect(id, module); });
        }
        menu.exec(menuPosition(menu, position));
    }

    QPoint menuPosition(QMenu& menu, QPoint preferred, QWidget* anchor = nullptr) const {
        const QSize size = menu.sizeHint();
        QRect bounds(owner.mapToGlobal(QPoint()), owner.size());
        if (auto* screen = QGuiApplication::screenAt(preferred))
            bounds = bounds.intersected(screen->availableGeometry());
        bounds.adjust(8, 8, -8, -8);
        const int maxX = std::max(bounds.left(), bounds.right() - size.width() + 1);
        const int maxY = std::max(bounds.top(), bounds.bottom() - size.height() + 1);
        if (preferred.y() > maxY && anchor)
            preferred.setY(anchor->mapToGlobal(QPoint()).y() - size.height());
        return {std::clamp(preferred.x(), bounds.left(), maxX),
                std::clamp(preferred.y(), bounds.top(), maxY)};
    }

    void showAddMenu(QWidget* anchor) {
        QMenu menu(anchor);
        for (auto* module : available) {
            const auto* info = &module->info;
            auto* action = menu.addAction(QStringLiteral("%1   %2").arg(
                QString::fromUtf8(info->iconGlyph), QString::fromUtf8(info->name)));
            QObject::connect(action, &QAction::triggered, &owner, [this, module] { addEffect(module, true); });
        }
        menu.exec(menuPosition(menu, anchor->mapToGlobal(QPoint(0, anchor->height())), anchor));
    }

    void syncModulePreset() {
        auto* effect = selected();
        if (!effect || !effect->module->presetCount || !effect->module->presetName) return;
        const auto current = effect->module->currentPreset
            ? effect->module->currentPreset(effect->instance) : -1;
        if (current >= 0 && current != modulePreset->currentIndex()) {
            const QSignalBlocker blocker(modulePreset);
            modulePreset->setCurrentIndex(current);
        }
    }

    void stepModulePreset(int direction) {
        if (modulePreset->count() < 2) return;
        modulePreset->setCurrentIndex((modulePreset->currentIndex() + direction + modulePreset->count())
                                      % modulePreset->count());
    }

    void showSelected() {
        auto* effect = selected();
        if (!effect) {
            moduleName->setText(QStringLiteral("模块设置"));
            { const QSignalBlocker blocker(modulePreset); modulePreset->clear(); }
            modulePower->setChecked(false);
            modulePower->setEnabled(false);
            if (auto* old = editorArea->takeWidget()) {
                old->hide();
                old->setParent(editorParking);
            }
            editorArea->setWidget(new Controls::TextLabel(QStringLiteral("选择 FX 模块")));
            return;
        }
        modulePower->setEnabled(master->isChecked());
        moduleName->setText(QStringLiteral("模块设置 - %1%2").arg(effect->name,
            selectedChannel<0?QString():QStringLiteral(" · %1").arg(selectedChannelLabel)));
        {
            const QSignalBlocker blocker(modulePreset);
            modulePreset->clear();
            if (effect->module->presetCount && effect->module->presetName) {
                const auto count = effect->module->presetCount();
                for (std::uint32_t index = 0; index < count; ++index)
                    modulePreset->addItem(QString::fromUtf8(effect->module->presetName(index)));
            }
        }
        modulePower->blockSignals(true);
        modulePower->setChecked(master->isChecked() && effect->enabled);
        modulePower->blockSignals(false);
        if (editorArea->widget()) {
            auto* old = editorArea->takeWidget();
            old->hide();
            old->setParent(editorParking);
        }
        if (!effect->editor) {
            effect->editor = effect->module->createEditor(effect->instance, editorArea->viewport());
        }
        if (effect->editor) {
            effect->module->selectChannel(effect->instance, selectedChannel);
            editorArea->setWidget(effect->editor);
        }
        else editorArea->setWidget(new Controls::TextLabel(QStringLiteral("无法加载 %1 控制界面").arg(effect->name)));
        syncModulePreset();
    }

    void refreshFlow() {
        chainContent->adjustSize();
        chainContent->update();
    }

    bool captureChain(std::vector<Common::MixerEffectState>& chain) {
        chain.clear();
        chain.reserve(static_cast<std::size_t>(list->count()));
        for (int row = 0; row < list->count(); ++row) {
            const auto* effect = effectFor(list->item(row)->data(Qt::UserRole).toInt());
            if (!effect) return false;
            const auto bytes = effect->module->snapshotSize();
            QByteArray snapshot(static_cast<qsizetype>(bytes), '\0');
            if (!effect->module->exportSnapshot(effect->instance, snapshot.data(), bytes)) return false;
            Common::MixerEffectState state;
            state.moduleId = QString::fromUtf8(effect->module->info.id);
            state.enabled = effect->enabled;
            state.preset = effect->module->currentPreset
                ? effect->module->currentPreset(effect->instance) : -1;
            state.parameters.insert(QStringLiteral("snapshot"),
                                    QString::fromLatin1(snapshot.toBase64()));
            chain.push_back(std::move(state));
        }
        for (const auto& [position, missing] : missingEffects)
            chain.insert(chain.begin() + std::min(position, chain.size()), missing);
        return true;
    }


    QString presetDirectory() const {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/fx_chain_presets");
    }

    bool readPreset(const QString& path, QString& name,
                    std::vector<Common::MixerEffectState>& chain, bool& enabled, QString& error) const {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
        if (file.size() > 16 * 1024 * 1024) { error = QStringLiteral("预设文件过大"); return false; }
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
        const auto object = document.object();
        if (parse.error != QJsonParseError::NoError || !document.isObject()
            || object.value("format").toString() != QStringLiteral("WinJACKNexus.FxChainPreset")
            || object.value("version").toDouble(-1) != 1
            || !object.value("name").isString() || object.value("name").toString().trimmed().isEmpty()
            || !object.value("enabled").isBool() || !object.value("effects").isArray()
            || object.value("effects").toArray().size() > 256) {
            error = QStringLiteral("不是有效的效果链预设或版本不支持"); return false;
        }
        std::vector<Common::MixerEffectState> result;
        for (const auto& value : object.value("effects").toArray()) {
            const auto effect = value.toObject();
            const auto preset = effect.value("preset").toDouble(-2);
            if (!value.isObject() || !effect.value("moduleId").isString()
                || effect.value("moduleId").toString().trimmed().isEmpty()
                || !effect.value("enabled").isBool() || !effect.value("parameters").isObject()
                || preset < -1 || preset > 2147483647 || preset != std::floor(preset)) {
                error = QStringLiteral("效果链模块数据无效"); return false;
            }
            Common::MixerEffectState state;
            state.moduleId = effect.value("moduleId").toString();
            state.enabled = effect.value("enabled").toBool();
            state.preset = static_cast<int>(preset);
            state.parameters = effect.value("parameters").toObject();
            const auto snapshot = state.parameters.value("snapshot");
            if (!snapshot.isUndefined() && (!snapshot.isString()
                || !QByteArray::fromBase64Encoding(snapshot.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors))) {
                error = QStringLiteral("模块参数快照无效"); return false;
            }
            result.push_back(std::move(state));
        }
        name = object.value("name").toString().trimmed();
        enabled = object.value("enabled").toBool();
        chain = std::move(result);
        return true;
    }

    void refreshPresets() {
        const QSignalBlocker blocker(presets);
        presets->clear();
        presets->addItem(QStringLiteral("当前效果链"), QString());
        const QDir directory(presetDirectory());
        for (const auto& entry : directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            QString name, error;
            bool enabled;
            std::vector<Common::MixerEffectState> chain;
            if (readPreset(entry.absoluteFilePath(), name, chain, enabled, error))
                presets->addItem(name, entry.absoluteFilePath());
            else qWarning().noquote() << "FX chain preset:" << entry.fileName() << error;
        }
        const int index = presets->findData(selectedPresetPath);
        presets->setCurrentIndex(std::max(0, index));
        if (index < 0) selectedPresetPath.clear();
    }

    void presetError(const QString& error) {
        QMessageBox::warning(&owner, QStringLiteral("效果链预设"), error);
    }

    void savePreset(bool saveAs) {
        QString path = selectedPresetPath;
        QString name = presets->currentText();
        if (saveAs || path.isEmpty()) {
            QDialog dialog(&owner);
            dialog.setObjectName(QStringLiteral("fxChainPresetNameDialog"));
            dialog.setWindowTitle(QStringLiteral("另存效果链预设"));
            auto* layout = new QVBoxLayout(&dialog);
            layout->addWidget(new Controls::TextLabel(QStringLiteral("预设名称"), &dialog));
            auto* field = new Controls::TextField(&dialog);
            field->setObjectName(QStringLiteral("fxChainPresetName"));
            field->setText(path.isEmpty() ? QString() : name);
            layout->addWidget(field);
            auto* actions = new QHBoxLayout;
            auto* accept = button(QStringLiteral("保存"), &dialog, "fxChainPresetNameSave");
            auto* cancel = button(QStringLiteral("取消"), &dialog);
            accept->setProperty("fxChainPresetAction", true);
            cancel->setProperty("fxChainPresetAction", true);
            accept->setEnabled(!field->text().trimmed().isEmpty());
            QObject::connect(field, &QLineEdit::textChanged, &dialog, [accept](const QString& text) {
                accept->setEnabled(!text.trimmed().isEmpty());
            });
            QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
            QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
            actions->addStretch(); actions->addWidget(accept); actions->addWidget(cancel);
            layout->addLayout(actions);
            dialog.resize(360, 140);
            field->setFocus();
            if (dialog.exec() != QDialog::Accepted) return;
            name = field->text().trimmed();
            path = presetDirectory() + QLatin1Char('/')
                + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        }
        std::vector<Common::MixerEffectState> chain;
        if (!captureChain(chain)) { presetError(QStringLiteral("无法读取模块参数，未保存预设")); return; }
        QJsonArray effects;
        for (const auto& effect : chain)
            effects.append(QJsonObject {{"moduleId", effect.moduleId}, {"enabled", effect.enabled},
                                        {"preset", effect.preset}, {"parameters", effect.parameters}});
        const QJsonObject object {{"format", QStringLiteral("WinJACKNexus.FxChainPreset")},
                                  {"version", 1}, {"name", name}, {"enabled", master->isChecked()},
                                  {"effects", effects}};
        if (!QDir().mkpath(presetDirectory())) { presetError(QStringLiteral("无法创建预设目录：%1").arg(presetDirectory())); return; }
        QSaveFile file(path);
        const auto bytes = QJsonDocument(object).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            presetError(QStringLiteral("保存失败：%1").arg(file.errorString())); return;
        }
        selectedPresetPath = path;
        refreshPresets();
    }

    void selectChainPreset(int index) {
        const auto path = presets->itemData(index).toString();
        if (path.isEmpty()) { selectedPresetPath.clear(); return; }
        QString name, error;
        bool enabled;
        std::vector<Common::MixerEffectState> chain;
        if (!readPreset(path, name, chain, enabled, error)) {
            presetError(error);
            const QSignalBlocker blocker(presets);
            presets->setCurrentIndex(std::max(0, presets->findData(selectedPresetPath)));
            return;
        }
        if (chainCommit && !chainCommit(chain, enabled)) {
            const QSignalBlocker blocker(presets);
            presets->setCurrentIndex(std::max(0, presets->findData(selectedPresetPath)));
            return;
        }
        loadChain(chain, enabled);
        selectedPresetPath = path;
    }

    void publish() {
        if (loading || !chainCommit) return;
        std::vector<Common::MixerEffectState> chain;
        if (captureChain(chain)) chainCommit(chain, master->isChecked());
    }

    void loadChain(const std::vector<Common::MixerEffectState>& chain, bool enabled) {
        loading = true;
        missingEffects.clear();
        while (!effects.isEmpty()) deleteEffect(effects.back().id);
        for (std::size_t position = 0; position < chain.size(); ++position) {
            const auto& state = chain[position];
            const auto module = std::find_if(available.cbegin(), available.cend(), [&state](const Module* value) {
                return QString::fromUtf8(value->info.id) == state.moduleId;
            });
            if (module == available.cend()) {
                missingEffects.emplace_back(position, state);
                continue;
            }
            auto* instance = (*module)->create(0);
            if (!instance) {
                missingEffects.emplace_back(position, state);
                continue;
            }
            const auto encoded = state.parameters.value(QStringLiteral("snapshot")).toString();
            const auto decoded = QByteArray::fromBase64Encoding(
                encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            const auto invalidSnapshot = !encoded.isEmpty() && (!decoded
                    || decoded.decoded.size() != static_cast<qsizetype>((*module)->snapshotSize())
                    || !(*module)->importSnapshot(instance, decoded.decoded.constData(),
                                                  static_cast<std::uint32_t>(decoded.decoded.size())));
            if (invalidSnapshot) {
                (*module)->destroy(instance);
                missingEffects.emplace_back(position, state);
                continue;
            }
            insertEffect(*module, instance, state.enabled, list->count());
        }
        master->setChecked(enabled);
        syncChainPower();
        if (!missingEffects.empty())
            moduleName->setText(QStringLiteral("部分 FX 无法加载，原参数已保留"));
        loading = false;
    }

    FxSettingsWindow& owner;
    QStringList sourceChannels;
    QHBoxLayout* channelRow {};
    QLabel* meterTitle {};
    std::function<void()> meterRefresh;
    std::vector<std::unique_ptr<Module>> loadedModules;
    int selectedChannel = -1;
    QString selectedChannelLabel {QStringLiteral("All")};
    QVector<Module*> available;
    QVector<Effect> effects;
    int nextId = 1;
    QListWidget* list {};
    QScrollArea* chainScroll {};
    FxChainCanvas* chainContent {};
    QScrollArea* editorArea {};
    QWidget* editorParking {};
    QLabel* moduleName {};
    QComboBox* modulePreset {};
    QComboBox* presets {};
    QString selectedPresetPath;
    Controls::ToggleButton* master {};
    QPushButton* modulePower {};
    std::function<void(bool)> chainEnabledHandler;
    std::function<bool(const std::vector<Common::MixerEffectState>&, bool)> chainCommit;
    std::function<float(bool)> meterRead;
    std::function<float(bool, std::size_t)> channelMeterRead;
    std::function<bool(std::size_t, float[8], float[8], float[8], void*, std::uint32_t)> liveAnalysisRead;
    bool loading {};
    std::vector<std::pair<std::size_t, Common::MixerEffectState>> missingEffects;
    QVector<QPushButton*> channelButtons;
    std::array<double, 8> simulatedInput { -24, -22, -28, -32, -25, -30, -27, -23 };
    std::array<double, 8> simulatedOutput { -26, -24, -30, -34, -27, -32, -29, -25 };
};

FxSettingsWindow::FxSettingsWindow(const QString& sourceName, const QStringList& sourceChannels, QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this, sourceName, sourceChannels)) {
    new ThemeBackgroundOverlay(QStringLiteral("sel-fx"), this);
    const auto updateOpacity = [this] {
        const auto opacity = ThemeBackground::componentOpacity(QStringLiteral("sel-fx"));
        for (auto* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (dynamic_cast<ThemeBackgroundOverlay*>(child)) continue;
            if (opacity < 1.0) {
                auto* effect = qobject_cast<QGraphicsOpacityEffect*>(child->graphicsEffect());
                if (!effect) { effect = new QGraphicsOpacityEffect(child); child->setGraphicsEffect(effect); }
                effect->setOpacity(opacity);
            } else if (child->graphicsEffect()) {
                child->setGraphicsEffect(nullptr);
            }
        }
        update();
    };
    updateOpacity();
    if (auto* core = ThemeManager::installedThemeCore())
        connect(core, &ThemeCore::themeChanged, this, updateOpacity);
}

void FxSettingsWindow::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    ThemeBackground::paint(painter, rect(), QStringLiteral("sel-fx"));
}

FxSettingsWindow::~FxSettingsWindow() {
    // Child controls emit signals while QWidget tears them down. Their slots
    // capture Impl, which is destroyed before QWidget's child hierarchy.
    for (auto* child : findChildren<QObject*>())
        QObject::disconnect(child, nullptr, this, nullptr);
    if (auto* core = ThemeManager::installedThemeCore())
        QObject::disconnect(core, nullptr, this, nullptr);
}
void FxSettingsWindow::setChainEnabled(bool enabled) {
    impl_->master->setChecked(enabled);
}
void FxSettingsWindow::setSourceChannels(const QStringList& channels) {
    impl_->setSourceChannels(channels);
}
void FxSettingsWindow::setChainEnabledHandler(std::function<void(bool)> handler) {
    impl_->chainEnabledHandler = std::move(handler);
}
void FxSettingsWindow::cloneChainFrom(const FxSettingsWindow& source) {
    if (&source != this) impl_->cloneChainFrom(*source.impl_);
}
void FxSettingsWindow::addDefaultMasterEffects() {
    const QStringList defaults = impl_->effects.isEmpty()
        ? QStringList {QStringLiteral("parametric-eq"), QStringLiteral("compressor"), QStringLiteral("limiter")}
        : QStringList {QStringLiteral("compressor"), QStringLiteral("limiter")};
    for (const auto& id : defaults) {
        const auto found = std::find_if(impl_->available.cbegin(), impl_->available.cend(), [&id](const auto* module) {
            return QString::fromUtf8(module->info.id) == id;
        });
        if (found != impl_->available.cend()) impl_->addEffect(*found, false);
    }
}
void FxSettingsWindow::setSourceName(const QString& name) {
    setWindowTitle(QStringLiteral("FX 设置 - %1").arg(name));
    if (auto* title=findChild<QLabel*>(QStringLiteral("fxSourceTitle"))) title->setText(windowTitle());
}
void FxSettingsWindow::loadChain(const std::vector<Common::MixerEffectState>& effects, bool enabled) {
    impl_->loadChain(effects, enabled);
}
void FxSettingsWindow::setChainCommitHandler(
    std::function<bool(const std::vector<Common::MixerEffectState>&, bool)> handler) {
    impl_->chainCommit = std::move(handler);
}
void FxSettingsWindow::setMeterReadHandler(std::function<float(bool)> handler) {
    impl_->meterRead = std::move(handler);
}
void FxSettingsWindow::setChannelMeterReadHandler(
    std::function<float(bool, std::size_t)> handler) {
    impl_->channelMeterRead = std::move(handler);
}
void FxSettingsWindow::setLiveAnalysisReadHandler(
    std::function<bool(std::size_t, float[8], float[8], float[8], void*, std::uint32_t)> handler) {
    impl_->liveAnalysisRead = std::move(handler);
}
}
