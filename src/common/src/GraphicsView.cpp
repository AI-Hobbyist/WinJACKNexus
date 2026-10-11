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
#include <WinJACKNexus/Common/GraphicsView.hpp>

#include <QGraphicsView>
#include <QOpenGLWidget>

namespace WinJACKNexus::Common
{

void configureAcceleratedGraphicsView(QGraphicsView &view)
{
    auto *viewport = new QOpenGLWidget;
    viewport->setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    view.setViewport(viewport);
    view.setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
}

}