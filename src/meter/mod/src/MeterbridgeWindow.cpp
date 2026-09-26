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
#include <WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp>

#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>
#include <WinJACKNexus/Meter/Mod/MeterHistoryWindow.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QByteArray>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace WinJACKNexus::Meter::Mod {

namespace {

int intervalForRefreshRate(int framesPerSecond) noexcept
{
    return std::max(1, 1000 / framesPerSecond);
}

}

MeterbridgeWindow::MeterbridgeWindow(MeterViewDataProvider& provider, QWidget* parent)
    : QMainWindow(parent)
    , provider_(provider)
{
    setObjectName(QStringLiteral("meterbridge.window"));
    setWindowTitle(QStringLiteral("WinJACKNexus - 电平表桥"));
    resize(1080, 740);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("meterbridge.tabs"));
    tabs_->setDocumentMode(true);
    tabs_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 0; background-color: #061523; }"
        "QTabWidget::tab-bar { left: 12px; }"
        "QTabBar { background-color: #061523; }"
        "QTabBar::tab { min-width: 104px; min-height: 28px; margin: 4px 4px 4px 0;"
        " padding: 2px 14px; border: 1px solid #244b6b; border-radius: 6px;"
        " background-color: #102742; color: #b8d7f6; }"
        "QTabBar::tab:selected { background-color: #176092; color: #ffffff; border-color: #31c6ff; }"
        "QTabBar::tab:disabled { color: #607a94; background-color: #0a1d30; }"));
    auto* central = new QWidget(tabs_);
    central->setObjectName(QStringLiteral("meterbridge.surface"));
    central->setStyleSheet(QStringLiteral(
        "QWidget#meterbridge.surface { background-color: #061523; color: #d8e9ff; }"
        "QScrollArea#meterbridge.cardScroll { background-color: #061523; border: 0; }"
        "QWidget#meterbridge.cards { background-color: #061523; }"
        "QLabel#meterbridge.emptyState { color: #93b7db; }"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);

    cardScrollArea_ = new WinJACKNexus::UI::Controls::ScrollField(central);
    cardScrollArea_->setObjectName(QStringLiteral("meterbridge.cardScroll"));
    cardScrollArea_->setWidgetResizable(true);
    cardScrollArea_->setFrameShape(QFrame::NoFrame);
    cardScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cardScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cardContainer_ = new QWidget(cardScrollArea_);
    cardContainer_->setObjectName(QStringLiteral("meterbridge.cards"));
    cardContainer_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    cardLayout_ = new QHBoxLayout(cardContainer_);
    cardLayout_->setContentsMargins(0, 0, 0, 0);
    cardLayout_->setSpacing(12);
    cardLayout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    cardScrollArea_->setWidget(cardContainer_);
    rootLayout->addWidget(cardScrollArea_, 1);

    emptyState_ = new WinJACKNexus::UI::Controls::TextLabel(QStringLiteral("暂无电平表探针"), central);
    emptyState_->setObjectName(QStringLiteral("meterbridge.emptyState"));
    emptyState_->setAlignment(Qt::AlignCenter);
    emptyState_->setMinimumHeight(180);
    rootLayout->addWidget(emptyState_, 1);
    tabs_->addTab(central, QStringLiteral("电平表桥"));
    historyPage_ = new MeterHistoryWindow(provider_,
                                          [this] { refresh(); },
                                          [this] { tabs_->setCurrentIndex(0); },
                                          tabs_);
    historyTabIndex_ = tabs_->addTab(historyPage_, QStringLiteral("历史图表"));
    tabs_->setTabEnabled(historyTabIndex_, false);
    setCentralWidget(tabs_);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(intervalForRefreshRate(refreshRate_));
    connect(refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
        auto& applicationSettings = WinJACKNexus::UI::ApplicationSettings::instance();
        connect(&applicationSettings,
            &WinJACKNexus::UI::ApplicationSettings::frameRateChanged,
            this,
            [this](int framesPerSecond) { applyRefreshRate(framesPerSecond); });
        applyRefreshRate(applicationSettings.frameRate());

    refresh();
}

void MeterbridgeWindow::refresh()
{
    const auto snapshots = provider_.snapshots(false);
    std::unordered_set<QString> expectedIds;
    expectedIds.reserve(snapshots.size());
    bool cardStructureChanged = false;
    for (const auto& snapshot : snapshots) {
        expectedIds.insert(snapshot.instanceId);
        if (!cards_.contains(snapshot.instanceId)) {
            addCard(snapshot);
            cardStructureChanged = true;
        }
        auto* card = cards_.value(snapshot.instanceId);
        card->setSnapshot(snapshot);
    }

    std::vector<QString> removedIds;
    removedIds.reserve(static_cast<std::size_t>(cards_.size()));
    for (auto iterator = cards_.cbegin(); iterator != cards_.cend(); ++iterator) {
        if (!expectedIds.contains(iterator.key()))
            removedIds.push_back(iterator.key());
    }
    for (const auto& instanceId : removedIds) {
        removeCard(instanceId);
        cardStructureChanged = true;
    }

    if (cardStructureChanged || cardOrder_ != renderedCardOrder_) {
        reorderCards(snapshots);
        renderedCardOrder_ = cardOrder_;
        int displayIndex = 1;
        for (const auto& instanceId : cardOrder_) {
            if (auto* card = cards_.value(instanceId, nullptr); card != nullptr)
                card->setDisplayIndex(displayIndex++);
        }
        updateCardContainerGeometry();
    }

    const auto hasCards = !cards_.isEmpty();
    cardScrollArea_->setVisible(hasCards);
    emptyState_->setVisible(!hasCards);
    tabs_->setTabEnabled(historyTabIndex_, hasCards);
    if (tabs_->currentIndex() == historyTabIndex_)
        historyPage_->setLiveSnapshots(snapshots);
    if (!hasCards && tabs_->currentIndex() == historyTabIndex_)
        tabs_->setCurrentIndex(0);
    if (hasCards && pendingRestoredHistorySelection_ && !selectedInstanceId_.isEmpty()) {
        pendingRestoredHistorySelection_ = false;
        openHistoryForInstanceId(selectedInstanceId_);
    }
}

int MeterbridgeWindow::cardCount() const noexcept
{
    return cards_.size();
}

MeterCard* MeterbridgeWindow::cardForInstanceId(const QString& instanceId) const
{
    return cards_.value(instanceId, nullptr);
}

MeterHistoryWindow* MeterbridgeWindow::historyWindowForInstanceId(const QString& instanceId) const
{
    return cards_.contains(instanceId) ? historyPage_ : nullptr;
}

void MeterbridgeWindow::openHistoryForInstanceId(const QString& instanceId)
{
    const auto snapshots = provider_.snapshots();
    const auto target = std::find_if(snapshots.cbegin(), snapshots.cend(), [&instanceId](const MeterViewSnapshot& snapshot) {
        return snapshot.instanceId == instanceId;
    });
    if (target == snapshots.cend())
        return;
    historyPage_->setSnapshots(snapshots, instanceId);
    selectedInstanceId_ = instanceId;
    restoredTabIndex_ = historyTabIndex_;
    tabs_->setTabEnabled(historyTabIndex_, true);
    tabs_->setCurrentIndex(historyTabIndex_);
    historyPage_->setFocus(Qt::OtherFocusReason);
}

void MeterbridgeWindow::selectInstance(const QString& instanceId)
{
    selectedInstanceId_ = instanceId;
    restoredTabIndex_ = tabs_->currentIndex();
    auto* card = cardForInstanceId(instanceId);
    if (card != nullptr)
        cardScrollArea_->ensureWidgetVisible(card, 12, 12);
}

int MeterbridgeWindow::refreshRate() const noexcept
{
    return refreshRate_;
}

int MeterbridgeWindow::refreshIntervalMilliseconds() const noexcept
{
    return intervalForRefreshRate(refreshRate_);
}

void MeterbridgeWindow::setRefreshRate(int framesPerSecond)
{
    WinJACKNexus::UI::ApplicationSettings::instance().setFrameRate(framesPerSecond);
}

void MeterbridgeWindow::applyRefreshRate(int framesPerSecond)
{
    if (refreshRate_ == framesPerSecond)
        return;
    refreshRate_ = framesPerSecond;
    refreshTimer_->setInterval(intervalForRefreshRate(refreshRate_));
    for (auto* card : cards_)
        card->setRefreshRate(refreshRate_);
    historyPage_->setRefreshRate(refreshRate_);
}

QJsonObject MeterbridgeWindow::projectState() const
{
    QJsonArray order;
    for (const auto& instanceId : cardOrder_)
        order.append(instanceId);
    return {
        { QStringLiteral("geometry"), QString::fromLatin1(saveGeometry().toBase64()) },
        { QStringLiteral("cardOrder"), order },
        { QStringLiteral("selectedMeter"), selectedInstanceId_ },
        { QStringLiteral("currentTab"), tabs_->currentIndex() }
    };
}

void MeterbridgeWindow::restoreProjectState(const QJsonObject& state)
{
    const auto geometry = QByteArray::fromBase64(state.value(QStringLiteral("geometry")).toString().toLatin1());
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    cardOrder_.clear();
    for (const auto& value : state.value(QStringLiteral("cardOrder")).toArray()) {
        const auto instanceId = value.toString();
        if (!instanceId.isEmpty() && !cardOrder_.contains(instanceId))
            cardOrder_.append(instanceId);
    }
    selectedInstanceId_ = state.value(QStringLiteral("selectedMeter")).toString();
    restoredTabIndex_ = state.value(QStringLiteral("currentTab")).toInt();
    pendingRestoredHistorySelection_ = restoredTabIndex_ == historyTabIndex_;
    refresh();
}

bool MeterbridgeWindow::isEmptyStateVisible() const noexcept
{
    return emptyState_->isVisible();
}

void MeterbridgeWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { updateCardContainerGeometry(); });
}

void MeterbridgeWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (refreshTimer_ != nullptr)
        refreshTimer_->start();
    QTimer::singleShot(0, this, [this] { updateCardContainerGeometry(); });
}

void MeterbridgeWindow::hideEvent(QHideEvent* event)
{
    if (refreshTimer_ != nullptr)
        refreshTimer_->stop();
    QMainWindow::hideEvent(event);
}

void MeterbridgeWindow::addCard(const MeterViewSnapshot& snapshot)
{
    auto* card = new MeterCard(provider_,
                               [this] { refresh(); },
                               [this](const QString& instanceId) { openHistoryForInstanceId(instanceId); },
                               cardContainer_);
    card->setRefreshRate(refreshRate_);
    cards_.insert(snapshot.instanceId, card);
    if (!cardOrder_.contains(snapshot.instanceId))
        cardOrder_.append(snapshot.instanceId);
    cardLayout_->addWidget(card);
}

void MeterbridgeWindow::removeCard(const QString& instanceId)
{
    auto* card = cards_.take(instanceId);
    if (card == nullptr)
        return;
    cardLayout_->removeWidget(card);
    cardOrder_.removeAll(instanceId);
    if (selectedInstanceId_ == instanceId)
        selectedInstanceId_.clear();
    delete card;
}

void MeterbridgeWindow::reorderCards(const std::vector<MeterViewSnapshot>& snapshots)
{
    for (const auto& snapshot : snapshots) {
        if (!cardOrder_.contains(snapshot.instanceId))
            cardOrder_.append(snapshot.instanceId);
    }
    for (auto iterator = cardOrder_.begin(); iterator != cardOrder_.end();) {
        if (!cards_.contains(*iterator))
            iterator = cardOrder_.erase(iterator);
        else
            ++iterator;
    }
    for (const auto& instanceId : cardOrder_) {
        auto* card = cards_.value(instanceId, nullptr);
        if (card == nullptr)
            continue;
        cardLayout_->removeWidget(card);
        cardLayout_->addWidget(card);
    }
}

void MeterbridgeWindow::updateCardContainerGeometry()
{
    const auto viewportHeight = cardScrollArea_->viewport()->height();
    if (viewportHeight > 0) {
        for (auto* card : cards_)
            card->setAvailableHeight(viewportHeight);
    }
    cardContainer_->setMinimumWidth(std::max(cardScrollArea_->viewport()->width(), cardLayout_->sizeHint().width()));
    cardContainer_->updateGeometry();
}

}
