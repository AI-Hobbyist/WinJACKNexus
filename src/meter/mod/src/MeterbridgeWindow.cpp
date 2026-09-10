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

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
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
    setWindowTitle(QStringLiteral("WinJACKNexus - Meterbridge"));
    resize(1080, 740);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);

    auto* previewControls = new QHBoxLayout;
    previewControls->setSpacing(8);
    scenarioSelector_ = new QComboBox(central);
    scenarioSelector_->setObjectName(QStringLiteral("meterbridge.previewScenario"));
    scenarioSelector_->setAccessibleName(QStringLiteral("Preview scenario"));
    scenarioSelector_->setToolTip(QStringLiteral("Switches the deterministic Meterbridge preview data"));
    customChannelCount_ = new QSpinBox(central);
    customChannelCount_->setObjectName(QStringLiteral("meterbridge.customChannelCount"));
    customChannelCount_->setRange(1, maximumMeterChannelCount);
    customChannelCount_->setAccessibleName(QStringLiteral("Custom channel count"));
    customChannelCount_->setToolTip(QStringLiteral("Sets the channel count only for the custom preview scenario"));
    previewProbeCount_ = new QSpinBox(central);
    previewProbeCount_->setObjectName(QStringLiteral("meterbridge.previewProbeCount"));
    previewProbeCount_->setRange(0, maximumMeterChannelCount);
    previewProbeCount_->setAccessibleName(QStringLiteral("Preview probe count"));
    previewProbeCount_->setToolTip(QStringLiteral("Adds or removes deterministic preview cards"));
    previewControls->addWidget(new QLabel(QStringLiteral("Preview"), central));
    previewControls->addWidget(scenarioSelector_, 1);
    previewControls->addWidget(new QLabel(QStringLiteral("Cards"), central));
    previewControls->addWidget(previewProbeCount_);
    previewControls->addWidget(new QLabel(QStringLiteral("Channels"), central));
    previewControls->addWidget(customChannelCount_);
    rootLayout->addLayout(previewControls);

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

    emptyState_ = new QLabel(QStringLiteral("No meter probes"), central);
    emptyState_->setObjectName(QStringLiteral("meterbridge.emptyState"));
    emptyState_->setAlignment(Qt::AlignCenter);
    emptyState_->setMinimumHeight(180);
    rootLayout->addWidget(emptyState_, 1);
    setCentralWidget(central);

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

    refresh();
}

void MeterbridgeWindow::refresh()
{
    const auto snapshots = provider_.snapshots();
    std::unordered_set<QString> expectedIds;
    expectedIds.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        expectedIds.insert(snapshot.instanceId);
        if (!cards_.contains(snapshot.instanceId))
            addCard(snapshot);
        cards_.value(snapshot.instanceId)->setSnapshot(snapshot);
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

bool MeterbridgeWindow::isEmptyStateVisible() const noexcept
{
    return emptyState_->isVisible();
}

void MeterbridgeWindow::addCard(const MeterViewSnapshot& snapshot)
{
    auto* card = new MeterCard(provider_, [this] { refresh(); }, cardContainer_);
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
    cardContainer_->setMinimumWidth(std::max(cardScrollArea_->viewport()->width(), cardLayout_->sizeHint().width()));
    cardContainer_->updateGeometry();
}

}