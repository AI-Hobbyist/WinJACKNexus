/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>

#include <QBoxLayout>
#include <QComboBox>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFrame>
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
    auto* result = new QPushButton(text, parent);
    result->setObjectName(QString::fromLatin1(objectName));
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

QFrame* panel(QWidget* parent) {
    auto* result = new QFrame(parent);
    result->setObjectName(QStringLiteral("panel"));
    return result;
}

class FxListDelegate final : public QStyledItemDelegate {
public:
    explicit FxListDelegate(std::function<void(int)> toggle,
                            std::function<void(int, QPoint)> menu, QObject* parent)
        : QStyledItemDelegate(parent), toggle_(std::move(toggle)), menu_(std::move(menu)) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF card = option.rect.adjusted(3, 3, -3, -3);
        const bool selected = option.state & QStyle::State_Selected;
        painter->setPen(QPen(selected ? QColor("#42cbfb") : QColor("#344d69"), selected ? 1.5 : 1.0));
        painter->setBrush(QColor(selected ? "#193b52" : "#182d43"));
        painter->drawRoundedRect(card, 7, 7);
        painter->setPen(QPen(QColor("#667d9a"), 1));
        painter->setBrush(QColor("#15273a"));
        painter->drawEllipse(QRectF(card.left() + 5, card.center().y() - 12, 24, 24));
        painter->setPen(QColor("#e8f2ff"));
        painter->drawText(QRectF(card.left() + 5, card.center().y() - 12, 24, 24),
                          Qt::AlignCenter, QString::number(index.row() + 1));
        painter->setPen(QColor("#7998bf"));
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 2; ++column)
                painter->drawEllipse(QPointF(card.left() + 39 + column * 6, card.top() + 20 + row * 7), 1.3, 1.3);
        const QColor accent(index.data(Qt::UserRole + 2).toString());
        const QRectF icon(card.left() + 58, card.center().y() - 24, 48, 48);
        painter->setPen(QPen(accent.lighter(130), 1));
        painter->setBrush(accent.darker(140));
        painter->drawRoundedRect(icon, 5, 5);
        QFont iconFont = painter->font(); iconFont.setPixelSize(28); iconFont.setBold(true); painter->setFont(iconFont);
        painter->setPen(Qt::white);
        painter->drawText(icon, Qt::AlignCenter, index.data(Qt::UserRole + 1).toString());
        QFont nameFont = painter->font(); nameFont.setPixelSize(15); nameFont.setBold(false); painter->setFont(nameFont);
        painter->drawText(QRectF(icon.right() + 13, card.top(), card.width() - 222, card.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        const bool enabled = index.data(Qt::UserRole + 3).toBool();
        const QRectF track(card.right() - 91, card.center().y() - 11, 42, 22);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(enabled ? "#187ee8" : "#38516d"));
        painter->drawRoundedRect(track, 11, 11);
        painter->setBrush(Qt::white);
        painter->drawEllipse(QRectF(enabled ? track.right() - 20 : track.left() + 2,
                                    track.top() + 2, 18, 18));
        painter->setPen(QColor("#a8c6ef"));
        QFont dotsFont = painter->font(); dotsFont.setPixelSize(24); painter->setFont(dotsFont);
        painter->drawText(QRectF(card.right() - 34, card.top(), 28, card.height()), Qt::AlignCenter, QStringLiteral("⋮"));
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
        const QRectF track(option.rect.right() - 94, option.rect.center().y() - 14, 48, 28);
        if (!track.contains(mouse->position())) return false;
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

    struct Module {
        QLibrary library;
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
    };

    struct Effect {
        int id {};
        Module* module {};
        QString name, glyph, color;
        WjnFxInstance* instance {};
        QWidget* editor {};
        bool enabled = false;
    };

    explicit Impl(FxSettingsWindow& window, const QString& source, const QStringList& sourceChannels)
        : owner(window), sourceChannels(sourceChannels) {
        owner.setWindowFlag(Qt::Window);
        owner.setWindowTitle(QStringLiteral("FX 设置 - %1").arg(source));
        owner.resize(1398, 874);
        owner.setMinimumSize(1100, 720);
        owner.setObjectName(QStringLiteral("fxWindow"));
        owner.setStyleSheet(QStringLiteral(R"(
            #fxWindow { background:#0b1929; color:#e9f2ff; font-family:'Microsoft YaHei UI'; font-size:15px; }
            QFrame#panel { background:#14283e; border:1px solid #29435d; border-radius:9px; }
            QLabel { color:#e5efff; background:transparent; }
            QPushButton, QToolButton, QComboBox { color:#d8e8ff; background:#192e45; border:1px solid #405e7f; border-radius:6px; padding:7px 13px; }
            QPushButton:hover, QToolButton:hover { border-color:#37b9fa; background:#214263; }
            QPushButton#primary, QPushButton:checked { border-color:#31c6ff; background:#153d60; color:#89e7ff; }
            QListWidget { background:transparent; border:0; outline:0; }
            QListWidget::item { background:#172c42; border:1px solid #304b69; border-radius:7px; margin:3px 2px; color:#f0f5ff; }
            QListWidget::item:selected { border:1px solid #38c9fc; background:#173d54; }
            QScrollArea { background:#10263a; border:1px solid #29435d; border-radius:7px; }
            QScrollArea#fxChainScroll { background:transparent; border:0; }
            QWidget#fxChainContent { background:transparent; }
            QPushButton#fxAddBottom { background:transparent; color:#8cbbfb; border:1px dashed #5d82a9; border-radius:7px; font-size:17px; padding:0; }
            QPushButton#fxAddBottom:hover { background:#1b3956; color:#bee5ff; border-color:#4bc8ff; }
            QScrollBar { background:#12263b; }
        )"));

        auto* root = new QVBoxLayout(&owner);
        root->setContentsMargins(6, 5, 6, 8);
        root->setSpacing(12);
        auto* header = panel(&owner);
        header->setFixedHeight(64);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(24, 5, 16, 5);
        auto* title = new QLabel(owner.windowTitle(), header);
        title->setObjectName(QStringLiteral("fxSourceTitle"));
        QFont titleFont = title->font(); titleFont.setPixelSize(24); titleFont.setBold(true); title->setFont(titleFont);
        headerLayout->addWidget(title);
        headerLayout->addStretch();
        headerLayout->addWidget(new QLabel(QStringLiteral("预设："), header));
        auto* presets = new QComboBox(header);
        presets->addItem(QStringLiteral("默认"));
        presets->setMinimumWidth(185);
        headerLayout->addWidget(presets);
        headerLayout->addWidget(button(QStringLiteral("保存"), header));
        headerLayout->addWidget(button(QStringLiteral("另存为"), header));
        root->addWidget(header);

        auto* body = new QHBoxLayout;
        body->setSpacing(14);
        root->addLayout(body, 1);
        auto* sidebar = panel(&owner);
        sidebar->setFixedWidth(400);
        auto* sideLayout = new QVBoxLayout(sidebar);
        sideLayout->setContentsMargins(13, 14, 13, 15);
        sideLayout->setSpacing(9);
        auto* sideHeader = new QHBoxLayout;
        auto* chainLabel = new QLabel(QStringLiteral("FX 链"), sidebar);
        QFont bold = chainLabel->font(); bold.setPixelSize(19); bold.setBold(true); chainLabel->setFont(bold);
        sideHeader->addWidget(chainLabel);
        master = new Controls::ToggleButton(sidebar);
        master->setObjectName(QStringLiteral("fxChainToggle"));
        master->setChecked(false);
        master->setToolTip(QStringLiteral("FX 链总开关"));
        QObject::connect(master, &QAbstractButton::toggled, &owner, [this](bool enabled) {
            syncChainPower();
            if (chainEnabledHandler) chainEnabledHandler(enabled);
        });
        sideHeader->addWidget(master);
        sideHeader->addStretch();
        auto* add = button(QStringLiteral("＋  添加模块"), sidebar, "primary");
        sideHeader->addWidget(add);
        sideLayout->addLayout(sideHeader);
        chainScroll = new QScrollArea(sidebar);
        chainScroll->setObjectName(QStringLiteral("fxChainScroll"));
        chainScroll->setFrameShape(QFrame::NoFrame);
        chainScroll->setWidgetResizable(true);
        chainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        chainScroll->viewport()->setStyleSheet(QStringLiteral("background:transparent;"));
        auto* chainContent = new QWidget(chainScroll);
        chainContent->setObjectName(QStringLiteral("fxChainContent"));
        auto* chainLayout = new QVBoxLayout(chainContent);
        chainLayout->setContentsMargins(0, 0, 0, 0);
        chainLayout->setSpacing(9);
        chainLayout->setSizeConstraint(QLayout::SetMinimumSize);
        list = new QListWidget(chainContent);
        list->setFrameShape(QFrame::NoFrame);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        list->setFixedHeight(0);
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setDragEnabled(true);
        list->setAcceptDrops(true);
        list->setDropIndicatorShown(true);
        list->setSpacing(3);
        list->setItemDelegate(new FxListDelegate([this](int id) {
            if (!master->isChecked()) return;
            for (auto& effect : effects) if (effect.id == id) {
                effect.enabled = !effect.enabled;
                for (int row = 0; row < list->count(); ++row)
                    if (list->item(row)->data(Qt::UserRole).toInt() == id)
                        refreshListItem(list->item(row));
                if (auto* current = selected(); current && current->id == id) {
                    modulePower->blockSignals(true);
                    modulePower->setChecked(effect.enabled);
                    modulePower->blockSignals(false);
                }
                break;
            }
        }, [this](int id, QPoint position) { showEffectMenu(id, position); }, list));
        chainLayout->addWidget(list);
        addBottom = button(QStringLiteral("＋  添加 FX 模块"), chainContent, "fxAddBottom");
        addBottom->setFixedHeight(58);
        chainLayout->addWidget(addBottom);
        chainLayout->addStretch();
        chainScroll->setWidget(chainContent);
        sideLayout->addWidget(chainScroll, 1);
        body->addWidget(sidebar);

        auto* right = new QVBoxLayout;
        right->setSpacing(12);
        body->addLayout(right, 1);
        auto* channelPanel = panel(&owner);
        channelPanel->setFixedHeight(124);
        auto* channelLayout = new QVBoxLayout(channelPanel);
        channelLayout->setContentsMargins(16, 12, 16, 14);
        auto* channelTitle = new QLabel(QStringLiteral("作用声道"), channelPanel);
        channelTitle->setFont(bold);
        channelLayout->addWidget(channelTitle);
        channelRow = new QHBoxLayout;
        QStringList visibleChannels { QStringLiteral("All") };
        visibleChannels.append(sourceChannels);
        const QStringList nativeChannels { QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
            QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs"),
            QStringLiteral("Lb"), QStringLiteral("Rb") };
        for (const auto& name : visibleChannels) {
            auto* choice = button(name, channelPanel);
            choice->setCheckable(true);
            choice->setMinimumWidth(61);
            choice->setChecked(name == QStringLiteral("All"));
            channelButtons.push_back(choice);
            const int channelIndex = name == QStringLiteral("All") ? -1 : nativeChannels.indexOf(name);
            QObject::connect(choice, &QPushButton::clicked, &owner, [this, choice, channelIndex] {
                for (auto* other : channelButtons) other->setChecked(other == choice);
                selectedChannel = channelIndex;
                for (auto& effect : effects)
                    if (effect.instance) effect.module->selectChannel(effect.instance, selectedChannel);
            });
            channelRow->addWidget(choice);
        }
        channelRow->addStretch();
        channelLayout->addLayout(channelRow);
        right->addWidget(channelPanel);

        auto* modulePanel = panel(&owner);
        auto* moduleLayout = new QVBoxLayout(modulePanel);
        moduleLayout->setContentsMargins(12, 10, 12, 10);
        auto* moduleHeader = new QHBoxLayout;
        modulePower = button(QStringLiteral("⏻"), modulePanel);
        modulePower->setObjectName(QStringLiteral("fxModulePower"));
        modulePower->setCheckable(true);
        modulePower->setFixedWidth(40);
        moduleHeader->addWidget(modulePower);
        moduleName = new QLabel(QStringLiteral("模块设置"), modulePanel);
        moduleName->setFont(bold);
        moduleHeader->addWidget(moduleName);
        moduleHeader->addStretch();
        moduleHeader->addWidget(new QLabel(QStringLiteral("预设："), modulePanel));
        modulePreset = new QComboBox(modulePanel);
        modulePreset->setObjectName(QStringLiteral("fxModulePreset"));
        modulePreset->setMinimumWidth(170);
        moduleHeader->addWidget(modulePreset);
        auto* previous = button(QStringLiteral("〈"), modulePanel);
        auto* next = button(QStringLiteral("〉"), modulePanel);
        moduleHeader->addWidget(previous);
        moduleHeader->addWidget(next);
        moduleLayout->addLayout(moduleHeader);
        editorArea = new QScrollArea(modulePanel);
        editorArea->setObjectName(QStringLiteral("fxEditorArea"));
        editorArea->setWidgetResizable(false);
        editorArea->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        editorParking = new QWidget(&owner);
        editorParking->hide();
        moduleLayout->addWidget(editorArea, 1);
        right->addWidget(modulePanel, 1);

        auto* bottom = new QHBoxLayout;
        bottom->setSpacing(12);
        auto* footer = panel(&owner);
        footer->setFixedHeight(136);
        auto* footerLayout = new QVBoxLayout(footer);
        footerLayout->setContentsMargins(13, 8, 13, 7);
        footerLayout->addWidget(new QLabel(QStringLiteral("信号流向："), footer));
        flowArea = new QScrollArea(footer);
        flowArea->setObjectName(QStringLiteral("fxSignalFlow"));
        flowArea->setFrameShape(QFrame::NoFrame);
        flowArea->setWidgetResizable(true);
        flowArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        flowArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        flowArea->setStyleSheet(QStringLiteral("QScrollArea { background:transparent; border:0; }"));
        flowContent = new QWidget(flowArea);
        flowContent->setStyleSheet(QStringLiteral("background:transparent;"));
        flowLayout = new QHBoxLayout(flowContent);
        flowLayout->setContentsMargins(4, 0, 4, 0);
        flowLayout->setSpacing(6);
        flowArea->setWidget(flowContent);
        footerLayout->addWidget(flowArea, 1);
        bottom->addWidget(footer, 1);
        auto* meterPanel = panel(&owner);
        meterPanel->setFixedWidth(290);
        auto* meterLayout = new QVBoxLayout(meterPanel);
        meterLayout->setContentsMargins(12, 7, 12, 7);
        auto* meterSelectors = new QHBoxLayout;
        meterSelectors->addWidget(new QLabel(QStringLiteral("输入 / 输出电平"), meterPanel));
        meterChannel = new QComboBox(meterPanel);
        meterChannel->addItems(visibleChannels);
        meterSelectors->addWidget(meterChannel);
        meterLayout->addLayout(meterSelectors);
        auto* meter = new FxLevelMeterWidget(meterPanel);
        meterLayout->addWidget(meter);
        auto updateMeters = [this, meter, nativeChannels] {
            const QString selected = meterChannel->currentText();
            const auto level = [&](const std::array<double, 8>& values) {
                if (selected != QStringLiteral("All")) {
                    const int index = nativeChannels.indexOf(selected);
                    return index >= 0 ? values[static_cast<std::size_t>(index)] : -60.0;
                }
                double power = 0.0;
                for (const auto& channel : this->sourceChannels) {
                    const int index = nativeChannels.indexOf(channel);
                    if (index >= 0) power += std::pow(10.0, values[static_cast<std::size_t>(index)] / 10.0);
                }
                return power > 0.0 ? 10.0 * std::log10(power / std::max(1.0, static_cast<double>(this->sourceChannels.size()))) : -60.0;
            };
            meter->setLevels(level(simulatedInput), level(simulatedOutput));
        };
        auto* meterTimer = new QTimer(meterPanel);
        QObject::connect(meterTimer, &QTimer::timeout, &owner, [this, updateMeters] {
            for (std::size_t index = 0; index < simulatedInput.size(); ++index) {
                const double target = -31.0 + QRandomGenerator::global()->bounded(28.0);
                simulatedInput[index] = std::clamp(simulatedInput[index] * .72 + target * .28, -60.0, 12.0);
                simulatedOutput[index] = std::clamp(simulatedInput[index] - 2.0, -60.0, 12.0);
            }
            updateMeters();
            syncModulePreset();
        });
        QObject::connect(meterChannel, &QComboBox::currentIndexChanged, &owner, [updateMeters] { updateMeters(); });
        meterTimer->start(90);
        updateMeters();
        bottom->addWidget(meterPanel);
        right->addLayout(bottom);

        QObject::connect(add, &QPushButton::clicked, &owner, [this, add] { showAddMenu(add); });
        QObject::connect(addBottom, &QPushButton::clicked, &owner, [this] { showAddMenu(addBottom); });
        QObject::connect(list, &QListWidget::currentItemChanged, &owner, [this] { showSelected(); });
        QObject::connect(list->model(), &QAbstractItemModel::rowsMoved, &owner, [this] { refreshFlow(); });
        QObject::connect(modulePower, &QPushButton::toggled, &owner, [this](bool enabled) {
            if (!master->isChecked()) return;
            if (auto* effect = selected()) { effect->enabled = enabled; refreshListItem(list->currentItem()); }
        });
        QObject::connect(modulePreset, &QComboBox::currentIndexChanged, &owner, [this](int index) {
            if (auto* effect = selected(); effect && effect->module->selectPreset && index >= 0) {
                effect->module->selectPreset(effect->instance, index);
                syncModulePreset();
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
            if (!version || version() != WJN_FX_API_VERSION || !count || count() != 1 || !meta
                || !candidate->create || !candidate->destroy || !candidate->selectChannel || !candidate->createEditor)
                continue;
            const auto* info = meta(0);
            if (!info || !(info->placements & WJN_FX_MIXER)) continue;
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
            editorArea->setWidget(new QLabel(QStringLiteral("找不到 Native Fx 模块：%1").arg(modulesPath)));
        }
    }

    ~Impl() {
        // Each instance owns its editor; remove it from the viewport before destruction.
        editorArea->takeWidget();
        for (const auto& effect : effects) if (effect.instance) effect.module->destroy(effect.instance);
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
        visibleChannels.append(channels);
        const QStringList nativeChannels { QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
            QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs"),
            QStringLiteral("Lb"), QStringLiteral("Rb") };
        for (const auto& name : visibleChannels) {
            auto* choice = button(name, channelRow->parentWidget());
            choice->setCheckable(true);
            choice->setMinimumWidth(61);
            choice->setChecked(name == QStringLiteral("All"));
            channelButtons.push_back(choice);
            const int channelIndex = name == QStringLiteral("All") ? -1 : nativeChannels.indexOf(name);
            QObject::connect(choice, &QPushButton::clicked, &owner, [this, choice, channelIndex] {
                for (auto* other : channelButtons) other->setChecked(other == choice);
                selectedChannel = channelIndex;
                for (auto& effect : effects)
                    if (effect.instance) effect.module->selectChannel(effect.instance, selectedChannel);
            });
            channelRow->addWidget(choice);
        }
        channelRow->addStretch();
        meterChannel->clear();
        meterChannel->addItems(visibleChannels);
        selectedChannel = -1;
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
            item->setData(Qt::UserRole + 1, effect.glyph);
            item->setData(Qt::UserRole + 2, effect.color);
            item->setData(Qt::UserRole + 3, master->isChecked() && effect.enabled);
            return;
        }
    }

    void updateChainListHeight() {
        const int rowHeight = list->count() ? list->sizeHintForRow(0) : 0;
        list->setFixedHeight(list->count() * (rowHeight + list->spacing()) + (list->count() ? 4 : 0));
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
        if (module->setTheme) {
            static const WjnFxTheme theme {"#0b1929", "#14283e", "#e9f2ff", "#31c6ff", "#29435d"};
            module->setTheme(instance, &theme);
        }
        effect.enabled = enabled;
        effects.push_back(effect);
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, effect.id);
        item->setSizeHint(QSize(250, 69));
        list->insertItem(row, item);
        updateChainListHeight();
        refreshListItem(item);
        list->setCurrentItem(item);
        refreshFlow();
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
        while (!effects.isEmpty()) deleteEffect(effects.back().id);
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
        updateChainListHeight();
        showSelected();
        refreshFlow();
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
        effect->editor = nullptr;
        effect->name = QString::fromUtf8(module->info.name);
        effect->glyph = QString::fromUtf8(module->info.iconGlyph);
        effect->color = QString::fromLatin1(module->info.iconColor);
        if (module->setTheme) {
            static const WjnFxTheme theme {"#0b1929", "#14283e", "#e9f2ff", "#31c6ff", "#29435d"};
            module->setTheme(instance, &theme);
        }
        refreshListItem(list->item(rowForEffect(id)));
        if (selected() && selected()->id == id) showSelected();
        refreshFlow();
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
        menu.exec(position);
    }

    void showAddMenu(QWidget* anchor) {
        QMenu menu(anchor);
        for (auto* module : available) {
            const auto* info = &module->info;
            auto* action = menu.addAction(QStringLiteral("%1   %2").arg(
                QString::fromUtf8(info->iconGlyph), QString::fromUtf8(info->name)));
            QObject::connect(action, &QAction::triggered, &owner, [this, module] { addEffect(module, true); });
        }
        menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
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
            editorArea->setWidget(new QLabel(QStringLiteral("选择 FX 模块")));
            return;
        }
        modulePower->setEnabled(master->isChecked());
        moduleName->setText(QStringLiteral("模块设置 - %1").arg(effect->name));
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
        else editorArea->setWidget(new QLabel(QStringLiteral("无法加载 %1 控制界面").arg(effect->name)));
        syncModulePreset();
    }

    void refreshFlow() {
        while (auto* item = flowLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const auto addChip = [this](const QString& glyph, const QString& title, const QString& color) {
            auto* chip = new QWidget(flowContent);
            chip->setFixedSize(95, 75);
            auto* layout = new QVBoxLayout(chip);
            layout->setContentsMargins(2, 0, 2, 0);
            layout->setSpacing(3);
            auto* icon = new QLabel(glyph, chip);
            icon->setFixedSize(52, 40);
            icon->setAlignment(Qt::AlignCenter);
            icon->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:6px;color:white;font-size:23px;font-weight:bold;")
                                    .arg(QColor(color).darker(145).name(), color));
            auto* nameLabel = new QLabel(title, chip);
            nameLabel->setAlignment(Qt::AlignCenter);
            nameLabel->setStyleSheet(QStringLiteral("color:#c9dcf7;font-size:11px;"));
            layout->addWidget(icon, 0, Qt::AlignHCenter);
            layout->addWidget(nameLabel);
            flowLayout->addWidget(chip);
        };
        const auto addArrow = [this] {
            auto* arrow = new QLabel(QStringLiteral("→"), flowContent);
            arrow->setFixedWidth(21);
            arrow->setAlignment(Qt::AlignCenter);
            arrow->setStyleSheet(QStringLiteral("color:#83a7d4;font-size:20px;"));
            flowLayout->addWidget(arrow);
        };
        flowLayout->addStretch();
        addChip(QStringLiteral("In"), QStringLiteral("输入"), QStringLiteral("#496783"));
        for (int i = 0; i < list->count(); ++i) {
            const int id = list->item(i)->data(Qt::UserRole).toInt();
            for (const auto& effect : effects) if (effect.id == id) {
                addArrow();
                addChip(effect.glyph, effect.name, effect.color);
            }
        }
        addArrow();
        addChip(QStringLiteral("Out"), QStringLiteral("输出"), QStringLiteral("#496783"));
        flowLayout->addStretch();
        flowContent->adjustSize();
    }

    FxSettingsWindow& owner;
    QStringList sourceChannels;
    QHBoxLayout* channelRow {};
    QComboBox* meterChannel {};
    std::vector<std::unique_ptr<Module>> loadedModules;
    int selectedChannel = -1;
    QVector<Module*> available;
    QVector<Effect> effects;
    int nextId = 1;
    QListWidget* list {};
    QScrollArea* chainScroll {};
    QPushButton* addBottom {};
    QScrollArea* editorArea {};
    QWidget* editorParking {};
    QLabel* moduleName {};
    QComboBox* modulePreset {};
    QScrollArea* flowArea {};
    QWidget* flowContent {};
    QHBoxLayout* flowLayout {};
    Controls::ToggleButton* master {};
    QPushButton* modulePower {};
    std::function<void(bool)> chainEnabledHandler;
    QVector<QPushButton*> channelButtons;
    std::array<double, 8> simulatedInput { -24, -22, -28, -32, -25, -30, -27, -23 };
    std::array<double, 8> simulatedOutput { -26, -24, -30, -34, -27, -32, -29, -25 };
};

FxSettingsWindow::FxSettingsWindow(const QString& sourceName, const QStringList& sourceChannels, QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this, sourceName, sourceChannels)) {}

FxSettingsWindow::~FxSettingsWindow() = default;
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
    for (const auto& id : {QStringLiteral("compressor"), QStringLiteral("limiter")}) {
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
}
