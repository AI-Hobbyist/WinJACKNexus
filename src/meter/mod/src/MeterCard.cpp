/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>
#include <WinJACKNexus/Meter/Mod/LocalizedDisplay.hpp>

#include <WinJACKNexus/Common/MeterLoudnessPresetCatalog.hpp>

#include <WinJACKNexus/UI/MeterLevelBar.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/UI/ThemePalette.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>

#include <WinJACKNexus/Common/MeterCustomLoudnessPresets.hpp>
#include <QAction>
#include <QAbstractItemView>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QMessageBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace WinJACKNexus::Meter::Mod {

namespace {

using WinJACKNexus::UI::bindLocalizedText;
using WinJACKNexus::UI::I18nManager;
QString cardText(const char* key, const char* english) {
    return WinJACKNexus::UI::localizedText("meterbridge", key, english);
}
WinJACKNexus::UI::Controls::TextLabel* cardLabel(const char* key, const char* english, QWidget* parent) {
    auto* result = new WinJACKNexus::UI::Controls::TextLabel({}, parent);
    bindLocalizedText(result, "text", "meterbridge", key, english);
    return result;
}


constexpr int minimumMeterCardHeight = 560;

QColor meterColor(const char* role, const char* fallback)
{
    return WinJACKNexus::UI::Controls::themeTokenColor(
        QStringLiteral("meter-bridge"), QString::fromLatin1(role), QColor(fallback));
}

QString meterCssColor(const char* role, const char* fallback)
{
    const auto value = meterColor(role, fallback);
    if (value.alpha() == 255) return value.name();
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(value.red()).arg(value.green()).arg(value.blue()).arg(value.alphaF());
}

QString stateText(MeterProbeState state)
{
    switch (state) {
    case MeterProbeState::silent:
        return cardText("state.silent", "Silent");
    case MeterProbeState::active:
        return cardText("state.active", "Active");
    case MeterProbeState::overload:
        return cardText("state.overload", "Overload");
    case MeterProbeState::offline:
        return cardText("state.offline", "Offline");
    }
    return cardText("state.unknown", "Unknown");
}

std::optional<double> asDouble(const std::optional<float>& value)
{
    if (!value.has_value())
        return std::nullopt;
    return static_cast<double>(*value);
}

WinJACKNexus::UI::ThemePalette meterCardPalette()
{
    auto palette = WinJACKNexus::UI::ThemePalette::darkRack();
    palette.setColor(QStringLiteral("window-bg"), meterColor("surface-window", "#0b1928"));
    palette.setColor(QStringLiteral("panel-bg"), meterColor("surface-window", "#0b1928"));
    palette.setColor(QStringLiteral("panel-raised"), meterColor("surface-raised", "#10253b"));
    palette.setColor(QStringLiteral("text"), meterColor("text-primary", "#e7f3ff"));
    palette.setColor(QStringLiteral("text-muted"), meterColor("text-muted", "#83a7d0"));
    palette.setColor(QStringLiteral("accent"), meterColor("accent-primary", "#3b9dff"));
    palette.setColor(QStringLiteral("accent-alt"), meterColor("accent-secondary", "#ffd143"));
    palette.setColor(QStringLiteral("border"), meterColor("border-card", "#2a5275"));
    palette.setColor(QStringLiteral("danger"), meterColor("state-danger", "#ff5269"));
    palette.setColor(QStringLiteral("success"), meterColor("state-success", "#43e88d"));
    return palette;
}

QToolButton* createCommandButton(QAction* action, const QString& objectName, QWidget* parent)
{
    auto* button = new WinJACKNexus::UI::Controls::IconActionButton(parent);
    button->setObjectName(objectName);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setFixedSize(68, 76);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setProperty("meterCardControl", QStringLiteral("action"));
    WinJACKNexus::UI::Controls::Palette colors;
    colors.panel = meterColor("control-action-surface-normal", "#13263b");
    colors.text = meterColor("control-action-text-normal", "#dbeaff");
    colors.border = meterColor("control-action-border-normal", "#274b6c");
    colors.accent = meterColor("accent-primary", "#3b9dff");
    WinJACKNexus::UI::Controls::applyPalette(button, colors);
    return button;
}

}

MeterCard::MeterCard(MeterViewDataProvider& provider,
                     std::function<void()> requestRefresh,
                     std::function<void(const QString&)> openHistory,
                     QWidget* parent)
    : QFrame(parent)
    , provider_(provider)
    , requestRefresh_(std::move(requestRefresh))
    , openHistory_(std::move(openHistory))
{
    setObjectName(QStringLiteral("meterbridge.card"));
    setProperty("meterCardSurface", true);
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground);
    setFixedSize(242, 834);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 12, 10, 10);
    rootLayout->setSpacing(8);
    const auto cardPalette = meterCardPalette();

    auto* headerPanel = new QFrame(this);
    headerPanel->setObjectName(QStringLiteral("meterCard.header"));
    headerPanel->setProperty("meterCardSection", QStringLiteral("header"));
    headerPanel->setAttribute(Qt::WA_StyledBackground);
    auto* headerLayout = new QHBoxLayout(headerPanel);
    headerLayout->setContentsMargins(8, 4, 8, 4);
    headerLayout->setSpacing(12);
    numberLabel_ = new WinJACKNexus::UI::Controls::TextLabel(QStringLiteral("1"), headerPanel);
    numberLabel_->setObjectName(QStringLiteral("meterCard.number"));
    numberLabel_->setProperty("meterCardRole", QStringLiteral("number"));
    numberLabel_->setAlignment(Qt::AlignCenter);
    numberLabel_->setFixedSize(36, 36);
    titleLabel_ = new WinJACKNexus::UI::Controls::TextLabel(headerPanel);
    titleLabel_->setObjectName(QStringLiteral("meterCard.clientName"));
    titleLabel_->setProperty("meterCardRole", QStringLiteral("title"));
    QFont titleFont = titleLabel_->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    titleLabel_->setFont(titleFont);
    layoutLabel_ = new WinJACKNexus::UI::Controls::TextLabel(headerPanel);
    layoutLabel_->setObjectName(QStringLiteral("meterCard.layout"));
    layoutLabel_->setProperty("meterCardRole", QStringLiteral("meta"));
    QFont layoutFont = layoutLabel_->font();
    layoutFont.setPointSize(layoutFont.pointSize() + 1);
    layoutLabel_->setFont(layoutFont);
    stateLabel_ = new WinJACKNexus::UI::Controls::TextLabel(headerPanel);
    stateLabel_->setObjectName(QStringLiteral("meterCard.state"));
    stateLabel_->setProperty("meterCardRole", QStringLiteral("state"));
    stateLabel_->hide();
    auto* headerText = new QWidget(headerPanel);
    auto* headerTextLayout = new QVBoxLayout(headerText);
    headerTextLayout->setContentsMargins(0, 0, 0, 0);
    headerTextLayout->setSpacing(0);
    headerTextLayout->addWidget(titleLabel_);
    headerTextLayout->addWidget(layoutLabel_);
    headerLayout->addWidget(numberLabel_);
    headerLayout->addWidget(headerText, 1);
    rootLayout->addWidget(headerPanel);

    auto* presetPanel = new QFrame(this);
    presetPanel->setObjectName(QStringLiteral("meterCard.presetPanel"));
    presetPanel->setProperty("meterCardSection", QStringLiteral("preset"));
    presetPanel->setAttribute(Qt::WA_StyledBackground);
    auto* presetLayout = new QHBoxLayout(presetPanel);
    presetLayout->setContentsMargins(0, 0, 0, 0);
    presetSelector_ = new WinJACKNexus::UI::Controls::ChoiceField(presetPanel);
    presetSelector_->setObjectName(QStringLiteral("meterCard.preset"));
    presetSelector_->setProperty("meterCardControl", QStringLiteral("preset"));
    bindLocalizedText(presetSelector_, "accessibleName", "meterbridge", "preset.accessible", "Loudness preset");
    bindLocalizedText(presetSelector_, "toolTip", "meterbridge", "preset.help", "Choose the display reference preset for this probe");
    presetSelector_->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
    auto presetFont = presetSelector_->font();
    presetFont.setPixelSize(16);
    presetSelector_->setFont(presetFont);
    WinJACKNexus::UI::Controls::Palette presetColors;
    presetColors.panel = meterColor("control-preset-surface-normal", "#12263c");
    presetColors.text = meterColor("control-preset-text-normal", "#dcecff");
    presetColors.border = meterColor("control-preset-border-normal", "#294d70");
    presetColors.accent = cardPalette.color(QStringLiteral("accent"));
    WinJACKNexus::UI::Controls::applyPalette(presetSelector_, presetColors);
    configurePresetSelector();
    presetLayout->addWidget(presetSelector_, 1);
    presetRefreshButton_ = new WinJACKNexus::UI::Controls::IconActionButton(presetPanel);
    presetRefreshButton_->setObjectName(QStringLiteral("meterCard.presetRefresh"));
    bindLocalizedText(presetRefreshButton_, "text", "meterbridge", "action.refresh", "Refresh");
    presetRefreshButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    presetRefreshButton_->setFixedHeight(WinJACKNexus::UI::Controls::sidebarControlHeight);
    presetLayout->addWidget(presetRefreshButton_);
    connect(presetRefreshButton_, &QToolButton::clicked, this, [this] {
        WinJACKNexus::Common::refreshCustomMeterLoudnessPresets();
        configurePresetSelector();
        if (requestRefresh_) requestRefresh_();
    });
    rootLayout->addWidget(presetPanel);

    auto* contentPanel = new QFrame(this);
    contentPanel->setObjectName(QStringLiteral("meterCard.content"));
    contentPanel->setProperty("meterCardSection", QStringLiteral("content"));
    contentPanel->setAttribute(Qt::WA_StyledBackground);
    auto* contentLayout = new QHBoxLayout(contentPanel);
    contentLayout->setContentsMargins(0, 12, 0, 8);
    contentLayout->setSpacing(12);
    auto* levelPanel = new QFrame(contentPanel);
    levelPanel->setObjectName(QStringLiteral("meterCard.levelPanel"));
    levelPanel->setProperty("meterCardSection", QStringLiteral("level"));
    levelPanel->setAttribute(Qt::WA_StyledBackground);
    levelPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    levelPanel->setFixedWidth(108);
    auto* levelLayout = new QVBoxLayout(levelPanel);
    levelLayout->setContentsMargins(0, 0, 0, 0);
    levelBar_ = new WinJACKNexus::UI::MeterLevelBar(levelPanel);
    levelBar_->setObjectName(QStringLiteral("meterCard.levelBar"));
    levelBar_->setRange(-72.0F, 12.0F);
    levelBar_->setThresholds(-18.0F, 0.0F);
    levelBar_->setMarks({
        { -72.0F, QStringLiteral("-72") },
        { -60.0F, QStringLiteral("-60") },
        { -50.0F, QStringLiteral("-50") },
        { -40.0F, QStringLiteral("-40") },
        { -30.0F, QStringLiteral("-30") },
        { -20.0F, QStringLiteral("-20") },
        { -10.0F, QStringLiteral("-10") },
        { 0.0F, QStringLiteral("0") },
        { 12.0F, QStringLiteral("+12") }
    });
    levelBar_->setUnit(QStringLiteral("dBFS"));
    levelBar_->setShowScaleLabels(true);
    levelBar_->setThemePalette(cardPalette);
    levelBar_->setProperty("wjnThemeScope", QStringLiteral("meter-bridge"));
    levelBar_->stopAnimation();
    levelLayout->addWidget(levelBar_);
    contentLayout->addWidget(levelPanel);

    auto* metricsPanel = new QFrame(contentPanel);
    metricsPanel->setObjectName(QStringLiteral("meterCard.metricsPanel"));
    metricsPanel->setProperty("meterCardSection", QStringLiteral("metrics"));
    metricsPanel->setAttribute(Qt::WA_StyledBackground);
    auto* metricsLayout = new QVBoxLayout(metricsPanel);
    metricsLayout->setContentsMargins(0, 0, 0, 0);
    metricsLayout->setSpacing(7);
    struct MetricDefinition {
        QString objectName;
        QString label;
        QString unit;
        QColor accent;
    };
    const std::array definitions {
        MetricDefinition { QStringLiteral("Peak"), cardText("metric.peak", "Peak"), QStringLiteral("dBFS"), meterColor("chart-series-peak-stroke", "#ff5269") },
        MetricDefinition { QStringLiteral("RMS"), cardText("metric.rms", "RMS"), QStringLiteral("dBFS"), meterColor("chart-series-rms-stroke", "#ff8a47") },
        MetricDefinition { QStringLiteral("dBTP"), QStringLiteral("dBTP"), QStringLiteral("dBTP"), meterColor("chart-series-true-peak-stroke", "#ffd143") },
        MetricDefinition { QStringLiteral("Momentary"), cardText("metric.momentary", "Momentary (M)"), QStringLiteral("LUFS"), meterColor("chart-series-momentary-stroke", "#43e88d") },
        MetricDefinition { QStringLiteral("Short-term"), cardText("metric.shortTerm", "Short-term (S)"), QStringLiteral("LUFS"), meterColor("chart-series-short-term-stroke", "#48c9ff") },
        MetricDefinition { QStringLiteral("Integrated"), cardText("metric.integrated", "Integrated (I)"), QStringLiteral("LUFS"), meterColor("chart-series-integrated-stroke", "#4b95ff") },
        MetricDefinition { QStringLiteral("LRA"), QStringLiteral("LRA"), QStringLiteral("LU"), meterColor("chart-series-lra-stroke", "#b96aff") }
    };
    std::array<WinJACKNexus::UI::MetricReadout**, 7> readoutTargets {
        &peakReadout_, &rmsReadout_, &truePeakReadout_, &momentaryReadout_, &shortTermReadout_, &integratedReadout_, &lraReadout_
    };
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto& definition = definitions[index];
        auto* readout = new WinJACKNexus::UI::MetricReadout(metricsPanel);
        readout->setObjectName(QStringLiteral("meterCard.readout.%1").arg(definition.objectName));
        readout->setLabel(definition.label);
        const auto refreshLabel = [readout, index] {
            switch (index) {
            case 0: readout->setLabel(cardText("metric.peak", "Peak")); break;
            case 1: readout->setLabel(cardText("metric.rms", "RMS")); break;
            case 3: readout->setLabel(cardText("metric.momentary", "Momentary (M)")); break;
            case 4: readout->setLabel(cardText("metric.shortTerm", "Short-term (S)")); break;
            case 5: readout->setLabel(cardText("metric.integrated", "Integrated (I)")); break;
            default: break;
            }
        };
        if (auto* manager = I18nManager::installed()) manager->bind(readout, refreshLabel);
        readout->setUnit(definition.unit);
        readout->setPrecision(1);
        readout->setPeakValueRefreshEnabled(index == 0 || index == 2);
        readout->setAccentColor(definition.accent);
        readout->setShowSurface(false);
        readout->setThemePalette(cardPalette);
        readout->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        *readoutTargets[index] = readout;
        metricsLayout->addWidget(readout, 1);
        if (index == 2) {
            auto* divider = new QFrame(metricsPanel);
            divider->setObjectName(QStringLiteral("meterCard.metricDivider"));
            divider->setProperty("meterCardSection", QStringLiteral("divider"));
            divider->setAttribute(Qt::WA_StyledBackground);
            divider->setFixedHeight(1);
            metricsLayout->addWidget(divider);
        }
    }
    contentLayout->addWidget(metricsPanel, 1);
    rootLayout->addWidget(contentPanel, 1);

    resetAction_ = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), cardText("action.reset", "Reset loudness"), this);
    bindLocalizedText(resetAction_, "text", "meterbridge", "action.reset", "Reset loudness");
    resetAction_->setObjectName(QStringLiteral("meterCard.resetAction"));
    bindLocalizedText(resetAction_, "toolTip", "meterbridge", "action.resetHelp", "Reset the current loudness session");
    recordingAction_ = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), cardText("action.record", "Record loudness"), this);
    recordingAction_->setObjectName(QStringLiteral("meterCard.recordingAction"));
    recordingAction_->setCheckable(true);
    bindLocalizedText(recordingAction_, "toolTip", "meterbridge", "action.recordHelp", "Start or stop loudness recording");
    historyAction_ = new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), cardText("action.history", "History chart"), this);
    bindLocalizedText(historyAction_, "text", "meterbridge", "action.history", "History chart");
    historyAction_->setObjectName(QStringLiteral("meterCard.historyAction"));
    bindLocalizedText(historyAction_, "toolTip", "meterbridge", "action.historyHelp", "Open the history chart for this probe");
    automaticResetAction_ = new QAction(cardText("action.autoReset", "Automatic reset"), this);
    bindLocalizedText(automaticResetAction_, "text", "meterbridge", "action.autoReset", "Automatic reset");
    automaticResetAction_->setObjectName(QStringLiteral("meterCard.automaticResetAction"));
    automaticResetAction_->setCheckable(true);
    locateAction_ = new QAction(cardText("action.locate", "Locate in patchbay"), this);
    bindLocalizedText(locateAction_, "text", "meterbridge", "action.locate", "Locate in patchbay");
    locateAction_->setObjectName(QStringLiteral("meterCard.locateAction"));

    auto* actionsPanel = new QFrame(this);
    actionsPanel->setObjectName(QStringLiteral("meterCard.actions"));
    actionsPanel->setProperty("meterCardSection", QStringLiteral("actions"));
    actionsPanel->setAttribute(Qt::WA_StyledBackground);
    auto* actionsLayout = new QHBoxLayout(actionsPanel);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(9);
    resetButton_ = createCommandButton(resetAction_, QStringLiteral("meterCard.resetButton"), actionsPanel);
    recordingButton_ = createCommandButton(recordingAction_, QStringLiteral("meterCard.recordingButton"), actionsPanel);
    historyButton_ = createCommandButton(historyAction_, QStringLiteral("meterCard.historyButton"), actionsPanel);
    actionsLayout->addWidget(resetButton_);
    actionsLayout->addWidget(recordingButton_);
    actionsLayout->addWidget(historyButton_);
    rootLayout->addWidget(actionsPanel);

    connect(presetSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (presetSelector_->itemData(index).toString() == QStringLiteral("custom"))
            return;
        dispatch(MeterViewCommandType::setPreset, std::nullopt, presetSelector_->itemData(index).toString());
    });
    // Activation also fires when the user clicks the already selected entry.
    // Snapshot updates and list refreshes must not open a creation dialog.
    connect(presetSelector_, &QComboBox::activated, this, [this](int index) {
        if (presetSelector_->itemData(index).toString() == QStringLiteral("custom"))
            createCustomPreset();
    });
    connect(resetAction_, &QAction::triggered, this, [this] {
        dispatch(MeterViewCommandType::resetLoudness);
    });
    connect(recordingAction_, &QAction::toggled, this, [this](bool enabled) {
        dispatch(MeterViewCommandType::setRecording, enabled);
    });
    connect(historyAction_, &QAction::triggered, this, [this] {
        dispatch(MeterViewCommandType::openHistory);
        if (!instanceId_.isEmpty() && openHistory_)
            openHistory_(instanceId_);
    });
    connect(automaticResetAction_, &QAction::toggled, this, [this](bool enabled) {
        dispatch(MeterViewCommandType::setAutomaticReset, enabled);
    });
    connect(locateAction_, &QAction::triggered, this, [this] {
        dispatch(MeterViewCommandType::selectInPatchbay);
    });
    if (auto* manager = I18nManager::installed())
        manager->bind(this, [this] { refreshLocalizedDisplay(); });
    applyTheme();
    if (auto* core = WinJACKNexus::UI::ThemeManager::installedThemeCore())
        connect(core, &WinJACKNexus::UI::ThemeCore::themeChanged,
                this, [this] { applyTheme(); });
}

void MeterCard::applyTheme()
{
    QString style = QStringLiteral(
        "QFrame[meterCardSurface=\"true\"] { background-color:%card-surface%; border:1px solid %card-border%; border-radius:8px; }"
        "QFrame[meterCardSection=\"header\"] { background-color:transparent; border:0; }"
        "QFrame[meterCardSection=\"preset\"], QFrame[meterCardSection=\"content\"],"
        "QFrame[meterCardSection=\"level\"], QFrame[meterCardSection=\"metrics\"],"
        "QFrame[meterCardSection=\"actions\"] { background-color:transparent; border:0; }"
        "QLabel[meterCardRole=\"number\"] { background-color:%number-surface%; color:%number-text%;"
        " border:1px solid %number-border%; border-radius:4px; font-size:17px; font-weight:600; }"
        "QLabel[meterCardRole=\"title\"] { color:%title-text%; }"
        "QLabel[meterCardRole=\"meta\"], QLabel[meterCardRole=\"presetLabel\"] { color:%preset-label%; }"
        "QLabel[meterCardRole=\"state\"] { color:%online-text%; background-color:%online-surface%;"
        " border:1px solid %online-border%; border-radius:8px; padding:2px 6px; }");
    style.replace(QStringLiteral("%card-surface%"), meterCssColor("surface-window", "#0b1928"));
    style.replace(QStringLiteral("%card-border%"), meterCssColor("border-card-outline", "#27516c"));
    style.replace(QStringLiteral("%number-surface%"), meterCssColor("control-number-surface", "#1688e8"));
    style.replace(QStringLiteral("%number-text%"), meterCssColor("control-number-text", "#071626"));
    style.replace(QStringLiteral("%number-border%"), meterCssColor("control-number-border", "#51b8ff"));
    style.replace(QStringLiteral("%title-text%"), meterCssColor("text-primary", "#e7f3ff"));
    style.replace(QStringLiteral("%preset-label%"), meterCssColor("text-preset-label", "#82a6cf"));
    style.replace(QStringLiteral("%online-text%"), meterCssColor("state-online-text", "#8ce0bf"));
    style.replace(QStringLiteral("%online-surface%"), meterCssColor("state-online-surface", "#1b3a34"));
    style.replace(QStringLiteral("%online-border%"), meterCssColor("state-online-border", "#3f8270"));
    setStyleSheet(style);

    const auto palette = meterCardPalette();
    if (levelBar_) levelBar_->setThemePalette(palette);
    const std::array<std::pair<WinJACKNexus::UI::MetricReadout*, std::pair<const char*, const char*>>, 7> readouts {{
        {peakReadout_, {"chart-series-peak-stroke", "#ff5269"}},
        {rmsReadout_, {"chart-series-rms-stroke", "#ff8a47"}},
        {truePeakReadout_, {"chart-series-true-peak-stroke", "#ffd143"}},
        {momentaryReadout_, {"chart-series-momentary-stroke", "#43e88d"}},
        {shortTermReadout_, {"chart-series-short-term-stroke", "#48c9ff"}},
        {integratedReadout_, {"chart-series-integrated-stroke", "#4b95ff"}},
        {lraReadout_, {"chart-series-lra-stroke", "#b96aff"}}
    }};
    for (const auto& [readout, color] : readouts) {
        if (!readout) continue;
        readout->setThemePalette(palette);
        readout->setAccentColor(meterColor(color.first, color.second));
    }
    WinJACKNexus::UI::Controls::Palette presetColors;
    presetColors.panel = meterColor("control-preset-surface-normal", "#12263c");
    presetColors.text = meterColor("control-preset-text-normal", "#dcecff");
    presetColors.border = meterColor("control-preset-border-normal", "#294d70");
    presetColors.accent = palette.color(QStringLiteral("accent"));
    WinJACKNexus::UI::Controls::applyPalette(presetSelector_, presetColors);
    auto* view = presetSelector_->view();
    auto popup = view->palette();
    popup.setColor(QPalette::Base, meterColor("control-preset-popup-surface", "#12263c"));
    popup.setColor(QPalette::Text, meterColor("control-preset-popup-text", "#dcecff"));
    popup.setColor(QPalette::Highlight, meterColor("control-preset-popup-selected-surface", "#24587c"));
    popup.setColor(QPalette::HighlightedText, meterColor("control-preset-popup-selected-text", "#ffffff"));
    view->setPalette(popup);
    view->setStyleSheet(QStringLiteral(
        "QAbstractItemView { background:%1; color:%2; selection-background-color:%3; selection-color:%4; border:1px solid %5; }"
        "QAbstractItemView::item { min-height:28px; }")
        .arg(meterCssColor("control-preset-popup-surface", "#12263c"),
             meterCssColor("control-preset-popup-text", "#dcecff"),
             meterCssColor("control-preset-popup-selected-surface", "#24587c"),
             meterCssColor("control-preset-popup-selected-text", "#ffffff"),
             meterCssColor("control-preset-popup-border", "#294d70")));
    WinJACKNexus::UI::Controls::Palette presetActions;
    presetActions.panel = meterColor("control-preset-action-surface", "#13263b");
    presetActions.text = meterColor("control-preset-action-text", "#dbeaff");
    presetActions.border = meterColor("control-preset-action-border", "#274b6c");
    presetActions.accent = palette.color(QStringLiteral("accent"));
    WinJACKNexus::UI::Controls::applyPalette(presetRefreshButton_, presetActions);

    WinJACKNexus::UI::Controls::Palette actionColors;
    actionColors.panel = meterColor("control-action-surface-normal", "#13263b");
    actionColors.text = meterColor("control-action-text-normal", "#dbeaff");
    actionColors.border = meterColor("control-action-border-normal", "#274b6c");
    actionColors.accent = palette.color(QStringLiteral("accent"));
    for (auto* button : {resetButton_, recordingButton_, historyButton_})
        WinJACKNexus::UI::Controls::applyPalette(button, actionColors);
}

void MeterCard::refreshLocalizedDisplay()
{
    stateLabel_->setText(stateText(displayState_));
    setToolTip(cardText("card.identityHelp", "Instance: %1\nName revision: %2").arg(instanceId_).arg(nameRevision_));
    layoutLabel_->setText(Localized::layoutName(layoutFallback_, displayChannelCount_, displayCustomLayout_));
    const QSignalBlocker presetBlocker(presetSelector_);
    for (int index = 0; index < presetSelector_->count(); ++index) {
        if (const auto* preset = Common::findMeterLoudnessPreset(presetSelector_->itemData(index).toString().toStdString()))
            presetSelector_->setItemText(index, Localized::presetName(*preset));
    }
    const QSignalBlocker blocker(recordingAction_);
    recordingAction_->setText(recordingAction_->isChecked()
        ? cardText("action.stopRecord", "Stop recording") : cardText("action.record", "Record loudness"));
}

void MeterCard::setSnapshot(const MeterViewSnapshot& snapshot)
{
    instanceId_ = snapshot.instanceId;
    clientName_ = snapshot.clientName;
    presetId_ = snapshot.presetId;
    if (presetRevision_ != WinJACKNexus::Common::customMeterLoudnessPresetRevision()) configurePresetSelector();
    titleLabel_->setText(snapshot.clientName);
    layoutFallback_ = snapshot.layoutLabel;
    displayChannelCount_ = snapshot.channels.size();
    displayCustomLayout_ = snapshot.channelLayoutEditable;
    layoutLabel_->setText(Localized::layoutName(snapshot));
    displayState_ = snapshot.state;
    nameRevision_ = snapshot.nameRevision;
    stateLabel_->setText(stateText(displayState_));
    setToolTip(cardText("card.identityHelp", "Instance: %1\nName revision: %2").arg(instanceId_).arg(nameRevision_));

    auto presetIndex = presetSelector_->findData(snapshot.presetId);
    if (presetIndex < 0) {
        presetSelector_->addItem(snapshot.presetId, snapshot.presetId);
        presetIndex = presetSelector_->count() - 1;
    }
    const QSignalBlocker presetBlocker(presetSelector_);
    presetSelector_->setCurrentIndex(presetIndex);
    const QSignalBlocker recordingBlocker(recordingAction_);
    recordingAction_->setChecked(snapshot.recordingEnabled);
    recordingAction_->setText(snapshot.recordingEnabled ? cardText("action.stopRecord", "Stop recording") : cardText("action.record", "Record loudness"));
    const QSignalBlocker automaticResetBlocker(automaticResetAction_);
    automaticResetAction_->setChecked(snapshot.automaticResetEnabled);

    levelBar_->setValue(snapshot.currentMetrics.peakDbfs);
    levelBar_->setPeakValue(snapshot.currentMetrics.truePeakDbtp);
    levelBar_->advanceAnimation();
    peakReadout_->setValue(asDouble(snapshot.currentMetrics.peakDbfs));
    rmsReadout_->setValue(asDouble(snapshot.currentMetrics.rmsDbfs));
    truePeakReadout_->setValue(asDouble(snapshot.currentMetrics.truePeakDbtp));
    momentaryReadout_->setValue(asDouble(snapshot.currentMetrics.momentaryLufs));
    shortTermReadout_->setValue(asDouble(snapshot.currentMetrics.shortTermLufs));
    integratedReadout_->setValue(asDouble(snapshot.currentMetrics.integratedLufs));
    lraReadout_->setValue(asDouble(snapshot.currentMetrics.lraLu));
}

void MeterCard::setDisplayIndex(int index)
{
    if (numberLabel_ != nullptr)
        numberLabel_->setText(QString::number(std::max(1, index)));
}

void MeterCard::setAvailableHeight(int height)
{
    const auto adjustedHeight = std::max(minimumMeterCardHeight, height);
    if (this->height() == adjustedHeight)
        return;
    setFixedHeight(adjustedHeight);
}

void MeterCard::setRefreshRate(int framesPerSecond)
{
    if (levelBar_ != nullptr)
        levelBar_->setFrameRate(framesPerSecond);
}

QString MeterCard::instanceId() const
{
    return instanceId_;
}

QString MeterCard::clientName() const
{
    return clientName_;
}

QString MeterCard::presetId() const
{
    return presetId_;
}

void MeterCard::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    menu.addAction(historyAction_);
    menu.addAction(automaticResetAction_);
    menu.addSeparator();
    menu.addAction(resetAction_);
    menu.addAction(recordingAction_);
    menu.addSeparator();
    menu.addAction(locateAction_);
    menu.exec(event->globalPos());
}

void MeterCard::dispatch(MeterViewCommandType type, std::optional<bool> enabled, const QString& presetId)
{
    if (instanceId_.isEmpty())
        return;
    MeterViewCommand command;
    command.type = type;
    command.instanceId = instanceId_;
    command.enabled = enabled;
    command.presetId = presetId;
    if (provider_.dispatch(command)) {
        if (type == MeterViewCommandType::setPreset) presetId_ = presetId;
        if (requestRefresh_) requestRefresh_();
    }
}

void MeterCard::createCustomPreset()
{
    namespace Controls = WinJACKNexus::UI::Controls;
    WinJACKNexus::Common::MeterCustomLoudnessPreset preset;
    if (const auto current = WinJACKNexus::Common::findCustomMeterLoudnessPreset(presetId_)) preset = *current;
    else if (const auto* current = WinJACKNexus::Common::findMeterLoudnessPreset(presetId_.toStdString())) {
        preset.targetIntegratedLufs = current->targetIntegratedLufs;
        preset.alternateTargetHighLufs = current->alternateTargetHighLufs;
        preset.maximumTruePeakDbtp = current->maximumTruePeakDbtp;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("meterCard.customPresetDialog"));
    bindLocalizedText(&dialog, "windowTitle", "meterbridge", "preset.createTitle", "Create a custom loudness reference");
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new Controls::TextField(&dialog);
    name->setObjectName(QStringLiteral("lufsPreset.name"));
    name->setText(preset.displayName);
    const auto field = [&](const char* id, double minimum, double maximum, double value, const QString& unit) {
        auto* result = new Controls::DecimalField(&dialog);
        result->setObjectName(QString::fromLatin1(id));
        result->setFixedHeight(Controls::sidebarControlHeight);
        result->setRange(minimum, maximum); result->setDecimals(1);
        result->setValue(value); result->setSuffix(unit);
        return result;
    };
    name->setFixedHeight(Controls::sidebarControlHeight);
    auto* target = field("lufsPreset.target", -70, 0, preset.targetIntegratedLufs, QStringLiteral(" LUFS"));
    auto* peak = field("lufsPreset.truePeak", -60, 12, preset.maximumTruePeakDbtp, QStringLiteral(" dBTP"));
    auto* range = new Controls::ToggleField(cardText("preset.range", "Enable target range"), &dialog);
    bindLocalizedText(range, "text", "meterbridge", "preset.range", "Enable target range");
    range->setObjectName(QStringLiteral("lufsPreset.range"));
    range->setFixedHeight(Controls::sidebarControlHeight);
    range->setChecked(preset.alternateTargetHighLufs.has_value());
    auto* upper = field("lufsPreset.upper", preset.targetIntegratedLufs, 0,
                        preset.alternateTargetHighLufs.value_or(preset.targetIntegratedLufs), QStringLiteral(" LUFS"));
    upper->setEnabled(range->isChecked());
    connect(range, &QCheckBox::toggled, upper, &QWidget::setEnabled);
    connect(target, qOverload<double>(&QDoubleSpinBox::valueChanged), upper, &QDoubleSpinBox::setMinimum);
    form->addRow(cardLabel("preset.name", "Name", &dialog), name);
    form->addRow(cardLabel("preset.lower", "Integrated loudness target/lower limit", &dialog), target);
    form->addRow(range); form->addRow(cardLabel("preset.upper", "Integrated loudness upper limit", &dialog), upper);
    form->addRow(cardLabel("preset.truePeak", "Maximum true peak", &dialog), peak);
    root->addLayout(form);
    auto* actions = new QHBoxLayout;
    auto* save = new Controls::ActionButton(cardText("action.save", "Save"), &dialog);
    bindLocalizedText(save, "text", "meterbridge", "action.save", "Save");
    save->setObjectName(QStringLiteral("lufsPreset.save"));
    auto* cancel = new Controls::ActionButton(cardText("action.cancel", "Cancel"), &dialog);
bindLocalizedText(cancel, "text", "meterbridge", "action.cancel", "Cancel");
        Controls::Palette colors;
    colors.panel = meterColor("control-preset-action-surface", "#13263b");
    colors.text = meterColor("control-preset-action-text", "#dbeaff");
    colors.border = meterColor("control-preset-action-border", "#274b6c");
    for (auto* action : {save, cancel}) {
        action->setFixedHeight(Controls::sidebarControlHeight); Controls::applyPalette(action, colors);
    }
    save->setEnabled(!name->text().trimmed().isEmpty());
    connect(name, &QLineEdit::textChanged, save, [save](const QString& text) { save->setEnabled(!text.trimmed().isEmpty()); });
    connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    actions->addStretch(); actions->addWidget(save); actions->addWidget(cancel); root->addLayout(actions);
    if (dialog.exec() == QDialog::Accepted) {
        preset.displayName = name->text().trimmed();
        preset.targetIntegratedLufs = target->value();
        preset.maximumTruePeakDbtp = peak->value();
        preset.alternateTargetHighLufs = range->isChecked() ? std::optional<double>(upper->value()) : std::nullopt;
        QString error;
        if (WinJACKNexus::Common::saveCustomMeterLoudnessPreset(preset, &error)) {
            configurePresetSelector();
            presetSelector_->setCurrentIndex(presetSelector_->findData(preset.id));
            return;
        }
        QMessageBox message(QMessageBox::Warning, {}, {}, QMessageBox::Ok, this);
        message.setTextFormat(Qt::PlainText);
        const auto refreshError = [&message, error] {
            if (error == QStringLiteral("预设名称或参考值无效"))
                message.setText(cardText("preset.invalid", "Invalid preset name or reference values"));
            else if (error.startsWith(QStringLiteral("无法创建目录：")))
                message.setText(cardText("preset.directoryFailure", "Unable to create directory: %1").arg(error.mid(QStringLiteral("无法创建目录：").size())));
            else message.setText(error);
        };
        if (auto* manager = I18nManager::installed()) manager->bind(&message, refreshError); else refreshError();
        bindLocalizedText(&message, "windowTitle", "meterbridge", "preset.saveError", "Loudness reference save failed");
        message.exec();
    }
    const QSignalBlocker blocker(presetSelector_);
    presetSelector_->setCurrentIndex(std::max(0, presetSelector_->findData(presetId_)));
}

void MeterCard::configurePresetSelector()
{
    const QSignalBlocker blocker(presetSelector_);
    presetSelector_->clear();
    for (const auto& preset : WinJACKNexus::Common::meterLoudnessPresetCatalog)
        presetSelector_->addItem(Localized::presetName(preset), QString::fromLatin1(preset.id));
    for (const auto& preset : WinJACKNexus::Common::customMeterLoudnessPresets())
        presetSelector_->addItem(preset.displayName, preset.id);
    presetSelector_->setCurrentIndex(std::max(0, presetSelector_->findData(presetId_)));
    presetRevision_ = WinJACKNexus::Common::customMeterLoudnessPresetRevision();
}

}
