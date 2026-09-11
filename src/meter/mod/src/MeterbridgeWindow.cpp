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

#include <WinJACKNexus/Meter/MockMeterDataProvider.hpp>
#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>
#include <WinJACKNexus/Meter/Mod/MeterHistoryWindow.hpp>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace WinJACKNexus::Meter::Mod {

MeterbridgeWindow::MeterbridgeWindow(MeterViewDataProvider& provider, QWidget* parent)
    : QMainWindow(parent)
    , provider_(provider)
{
    setObjectName(QStringLiteral("meterbridge.window"));
    setWindowTitle(QStringLiteral("WinJACKNexus - 电平表桥"));
    resize(1080, 740);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("meterbridge.tabs"));
    tabs_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 1px solid #183b5a; background-color: #061523; }"
        "QTabBar::tab { background-color: #0a1d30; color: #9fc1e4; border: 1px solid #183b5a; padding: 7px 18px; }"
        "QTabBar::tab:selected { background-color: #103c68; color: #ffffff; border-color: #2f9eff; }"));
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

    auto* previewControlsWidget = new QWidget(central);
    previewControlsWidget->setObjectName(QStringLiteral("meterbridge.previewControls"));
    auto* previewControls = new QHBoxLayout(previewControlsWidget);
    previewControls->setContentsMargins(0, 0, 0, 0);
    previewControls->setSpacing(8);
    scenarioSelector_ = new QComboBox(central);
    scenarioSelector_->setObjectName(QStringLiteral("meterbridge.previewScenario"));
    scenarioSelector_->setAccessibleName(QStringLiteral("预览场景"));
    scenarioSelector_->setToolTip(QStringLiteral("切换确定性的电平表桥预览数据"));
    customChannelCount_ = new QSpinBox(central);
    customChannelCount_->setObjectName(QStringLiteral("meterbridge.customChannelCount"));
    customChannelCount_->setRange(1, maximumMeterChannelCount);
    customChannelCount_->setAccessibleName(QStringLiteral("自定义声道数量"));
    customChannelCount_->setToolTip(QStringLiteral("只为自定义预览场景设置声道数量"));
    previewProbeCount_ = new QSpinBox(central);
    previewProbeCount_->setObjectName(QStringLiteral("meterbridge.previewProbeCount"));
    previewProbeCount_->setRange(0, maximumMeterChannelCount);
    previewProbeCount_->setAccessibleName(QStringLiteral("预览探针数量"));
    previewProbeCount_->setToolTip(QStringLiteral("添加或移除确定性的预览卡片"));
    previewControls->addWidget(new QLabel(QStringLiteral("预览场景"), central));
    previewControls->addWidget(scenarioSelector_, 1);
    previewControls->addWidget(new QLabel(QStringLiteral("卡片数"), central));
    previewControls->addWidget(previewProbeCount_);
    previewControls->addWidget(new QLabel(QStringLiteral("声道数"), central));
    previewControls->addWidget(customChannelCount_);
    rootLayout->addWidget(previewControlsWidget);

    cardScrollArea_ = new QScrollArea(central);
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

    emptyState_ = new QLabel(QStringLiteral("暂无电平表探针"), central);
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

    if (auto* previewProvider = dynamic_cast<MockMeterDataProvider*>(&provider_); previewProvider != nullptr) {
        for (const auto scenario : previewProvider->availableScenarios())
            scenarioSelector_->addItem(MockMeterDataProvider::scenarioDisplayName(scenario), static_cast<int>(scenario));
        scenarioSelector_->setCurrentIndex(scenarioSelector_->findData(static_cast<int>(previewProvider->scenario())));
        customChannelCount_->setValue(previewProvider->customChannelCount());
        previewProbeCount_->setValue(previewProvider->previewProbeCount());
        connect(scenarioSelector_, &QComboBox::currentIndexChanged, this, [this, previewProvider](int index) {
            previewProvider->setScenario(static_cast<MockMeterScenario>(scenarioSelector_->itemData(index).toInt()));
            refresh();
        });
        connect(customChannelCount_, qOverload<int>(&QSpinBox::valueChanged), this, [this, previewProvider](int count) {
            previewProvider->setCustomChannelCount(static_cast<std::uint16_t>(count));
            refresh();
        });
        connect(previewProbeCount_, qOverload<int>(&QSpinBox::valueChanged), this, [this, previewProvider](int count) {
            previewProvider->setPreviewProbeCount(static_cast<std::uint16_t>(count));
            refresh();
        });
    } else {
        scenarioSelector_->hide();
        customChannelCount_->hide();
        previewProbeCount_->hide();
    }
    previewControlsWidget->hide();

    refresh();
}

void MeterbridgeWindow::refresh()
{
    const auto snapshots = provider_.snapshots();
    std::unordered_set<QString> expectedIds;
    expectedIds.reserve(snapshots.size());
    int displayIndex = 1;
    for (const auto& snapshot : snapshots) {
        expectedIds.insert(snapshot.instanceId);
        if (!cards_.contains(snapshot.instanceId))
            addCard(snapshot);
        auto* card = cards_.value(snapshot.instanceId);
        card->setSnapshot(snapshot);
        card->setDisplayIndex(displayIndex++);
    }

    std::vector<QString> removedIds;
    removedIds.reserve(static_cast<std::size_t>(cards_.size()));
    for (auto iterator = cards_.cbegin(); iterator != cards_.cend(); ++iterator) {
        if (!expectedIds.contains(iterator.key()))
            removedIds.push_back(iterator.key());
    }
    for (const auto& instanceId : removedIds)
        removeCard(instanceId);

    const auto hasCards = !cards_.isEmpty();
    cardScrollArea_->setVisible(hasCards);
    emptyState_->setVisible(!hasCards);
    historyPage_->setSnapshots(snapshots);
    tabs_->setTabEnabled(historyTabIndex_, hasCards);
    if (!hasCards && tabs_->currentIndex() == historyTabIndex_)
        tabs_->setCurrentIndex(0);
    updateCardContainerGeometry();
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
    tabs_->setTabEnabled(historyTabIndex_, true);
    tabs_->setCurrentIndex(historyTabIndex_);
    historyPage_->setFocus(Qt::OtherFocusReason);
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
    QTimer::singleShot(0, this, [this] { updateCardContainerGeometry(); });
}

void MeterbridgeWindow::addCard(const MeterViewSnapshot& snapshot)
{
    auto* card = new MeterCard(provider_,
                               [this] { refresh(); },
                               [this](const QString& instanceId) { openHistoryForInstanceId(instanceId); },
                               cardContainer_);
    cards_.insert(snapshot.instanceId, card);
    cardLayout_->addWidget(card);
}

void MeterbridgeWindow::removeCard(const QString& instanceId)
{
    auto* card = cards_.take(instanceId);
    if (card == nullptr)
        return;
    cardLayout_->removeWidget(card);
    delete card;
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