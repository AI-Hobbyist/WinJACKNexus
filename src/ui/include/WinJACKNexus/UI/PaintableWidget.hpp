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
#pragma once

#include <WinJACKNexus/UI/Export.hpp>

#include <QWidget>

class QPainter;

namespace WinJACKNexus::UI {

class WINJACK_UI_API PaintableWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool renderingActive READ renderingActive WRITE setRenderingActive NOTIFY renderingActiveChanged)
    Q_PROPERTY(QString wjnRole READ wjnRole WRITE setWjnRole)

public:
    explicit PaintableWidget(QWidget* parent = nullptr);
    ~PaintableWidget() override;

    bool renderingActive() const noexcept;
    bool isRenderingActive() const noexcept;
    void setRenderingActive(bool active);

    QString wjnRole() const;
    void setWjnRole(const QString& role);

    void requestRepaint();

Q_SIGNALS:
    void renderingActiveChanged(bool active);
    void repaintRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

    virtual void paintContent(QPainter& painter) = 0;
    bool isRenderingAllowed() const noexcept;
    virtual void renderingStateChanged() noexcept;

private:
    bool renderingActive_ = true;
    QString wjnRole_;
};

}