/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Editor.hpp"
#include <QApplication>
#include <QDialog>
#include <QFormLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QUuid>
#include <QVBoxLayout>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <algorithm>

namespace WinJACKNexus::SignalSwitch
{
namespace
{
namespace C = UI::Controls;
QString text(const char *key, const char *english)
{
    return UI::localizedText("signalSwitch", key, english);
}

void dialogButtons(QDialog &dialog, QVBoxLayout *layout)
{
    auto *buttons = new C::DialogButtonBar(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
}

QAction *action(QMenu &menu, const char *key, const char *english, std::function<void()> apply)
{
    auto *item = menu.addAction(QString());
    UI::bindLocalizedText(item, "text", "signalSwitch", key, english);
    item->setObjectName(QString("signalSwitch.") + key);
    QObject::connect(item, &QAction::triggered, &menu, std::move(apply));
    return item;
}

QString freshId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace

bool Editor::eventFilter(QObject *object, QEvent *event)
{
    if (service_.state.locked)
        return QWidget::eventFilter(object, event);
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget)
        return false;
    const auto id = widget->property("signalSwitchId").toString();
    const auto group = widget->property("signalSwitchGroup").toString();
    const bool canvas = widget == canvas_;
    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        pressPosition_ = mouse->globalPosition().toPoint();
        dragging_ = false;
        dragStarts_.clear();
        if (canvas)
        {
            selecting_ = true;
            if (!(mouse->modifiers() & Qt::ControlModifier))
                selected_.clear();
            selectionBase_ = selected_;
            rubber_->setGeometry(QRect(mouse->position().toPoint(), QSize()));
            rubber_->show();
            return true;
        }
        if (!group.isEmpty())
            selected_ =
                QSet<QString>(service_.state.groups[group].members.begin(), service_.state.groups[group].members.end());
        else if (!id.isEmpty())
        {
            if (mouse->modifiers() & Qt::ControlModifier)
            {
                if (selected_.contains(id))
                    selected_.remove(id);
                else
                    selected_.insert(id);
                refresh();
                return true;
            }
            if (!selected_.contains(id))
                selected_ = {id};
        }
        for (const auto &member : selected_)
            dragStarts_[member] = service_.state.cards.value(member).geometry.topLeft();
        return !group.isEmpty();
    }
    if (event->type() == QEvent::MouseMove)
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!(mouse->buttons() & Qt::LeftButton))
            return false;
        if (selecting_)
        {
            const auto start = canvas_->mapFromGlobal(pressPosition_);
            rubber_->setGeometry(QRect(start, mouse->position().toPoint()).normalized());
            selected_ = selectionBase_;
            for (auto it = service_.state.cards.begin(); it != service_.state.cards.end(); ++it)
            {
                if (rubber_->geometry().intersects(it->geometry))
                    selected_.insert(it.key());
            }
            refresh();
            rubber_->raise();
            return true;
        }
        if (dragStarts_.isEmpty())
            return false;
        auto delta = mouse->globalPosition().toPoint() - pressPosition_;
        if (!dragging_ && delta.manhattanLength() < QApplication::startDragDistance())
            return false;
        dragging_ = true;
        if (auto *button = qobject_cast<QAbstractButton *>(widget))
            button->setDown(false);
        for (auto it = dragStarts_.begin(); it != dragStarts_.end(); ++it)
        {
            delta.setX(std::max(delta.x(), -it->x()));
            delta.setY(std::max(delta.y(), -it->y()));
        }
        for (auto it = dragStarts_.begin(); it != dragStarts_.end(); ++it)
            service_.state.cards[it.key()].geometry.moveTopLeft(it.value() + delta);
        refresh();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        if (selecting_)
        {
            selecting_ = false;
            rubber_->hide();
            return true;
        }
        if (dragging_)
        {
            const auto anchor = dragStarts_.firstKey();
            const auto position = service_.state.cards[anchor].geometry.topLeft();
            const auto snap = service_.state.snapped(position) - position;
            for (const auto &member : selected_)
                service_.state.cards[member].geometry.translate(snap);
            dragging_ = false;
            dragStarts_.clear();
            service_.notify(true);
            return true;
        }
        dragStarts_.clear();
        return !group.isEmpty() || bool(mouse->modifiers() & Qt::ControlModifier);
    }
    return QWidget::eventFilter(object, event);
}

void Editor::refreshGroups()
{
    for (auto it = groupPanels_.begin(); it != groupPanels_.end();)
    {
        if (!service_.state.groups.contains(it.key()))
        {
            delete it.value();
            it = groupPanels_.erase(it);
        }
        else
            ++it;
    }
    const int padding = style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
    for (auto it = service_.state.groups.begin(); it != service_.state.groups.end(); ++it)
    {
        auto *panel = groupPanels_.value(it.key());
        if (!panel)
        {
            panel = new C::GroupPanel({}, canvas_);
            panel->setObjectName("signalSwitchGroup." + it.key());
            panel->setProperty("signalSwitchGroup", it.key());
            panel->installEventFilter(this);
            panel->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(panel, &QWidget::customContextMenuRequested, this,
                    [this, id = it.key(), panel](QPoint point) { showGroupMenu(id, panel->mapToGlobal(point)); });
            groupPanels_[it.key()] = panel;
        }
        QRect bounds;
        for (const auto &id : it->members)
            bounds = bounds.united(service_.state.cards.value(id).geometry);
        panel->setTitle(it->name);
        panel->setGeometry(bounds.adjusted(-padding, -C::sidebarControlHeight, padding, padding));
        panel->setVisible(!service_.state.locked);
        panel->lower();
    }
}

bool Editor::editText(const char *key, const char *english, QString *value)
{
    QDialog dialog(this);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", key, english);
    auto *layout = new QVBoxLayout(&dialog);
    auto *field = new C::TextField(&dialog);
    field->setObjectName("signalSwitch.textInput");
    field->setText(*value);
    layout->addWidget(field);
    dialogButtons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted || field->text().trimmed().isEmpty())
        return false;
    *value = field->text().trimmed();
    return true;
}

void Editor::reportConflict()
{
    UI::bindLocalizedText(
        status_, "text", "signalSwitch", "conflict",
        "The selection conflicts with an existing group or relation. Remove the previous membership first.");
    status_->show();
}

void Editor::showCardMenu(const QString &id, const QPoint &global)
{
    if (!selected_.contains(id))
        selected_ = {id};
    refresh();
    QMenu menu(this);
    menu.setObjectName("signalSwitchCardMenu");
    const auto members = selected_.values();
    QMap<QString, bool> current, proposed;
    for (const auto &endpoint : service_.endpoints)
        current[endpoint.id] = endpoint.desired;
    const bool switchesOnly = std::all_of(members.begin(), members.end(), [this](const auto &member) {
        const auto *endpoint = service_.find(member);
        return endpoint && !endpoint->sound && !endpoint->peak && !endpoint->gainLinear;
    });
    if (switchesOnly)
    {
        auto *on = action(menu, "on", "On", [this, members] { service_.setEnabled(members, true); });
        on->setEnabled(service_.state.resolve(current, members, true, &proposed));
        action(menu, "off", "Off", [this, members] { service_.setEnabled(members, false); });
    }
    if (members.size() == 1)
    {
        const auto *endpoint = service_.find(id);
        if (endpoint && endpoint->sound)
        {
            action(menu, "trigger", "Play", [this, id] { service_.activate(id); });
            action(menu, "loadSound", "Load sound…", [this, id] { loadSound(id); });
            action(menu, "shortcut", "Global shortcut…", [this, id] { editShortcut(id); });
        }
        if (endpoint && endpoint->gainLinear)
            action(menu, "gainRange", "Gain range…", [this, id] { editGainRange(id); });
        if (endpoint && endpoint->peak)
        {
            auto *direction = menu.addMenu(QString());
            UI::bindLocalizedText(direction, "title", "signalSwitch", "meterDirection", "Meter direction");
            for (const bool vertical : {false, true})
            {
                auto *item = action(*direction, vertical ? "vertical" : "horizontal", vertical ? "Vertical" : "Horizontal",
                                    [this, id, vertical] {
                    if (auto *current = service_.find(id))
                    {
                        current->peakVertical = vertical;
                        service_.notify(true);
                    }
                });
                item->setCheckable(true);
                item->setChecked(endpoint->peakVertical == vertical);
            }
        }
    }
    if (members.size() == 1)
        action(menu, "rename", "Rename", [this, id] { rename(id); });
    menu.addSeparator();
    action(menu, "size", "Width and height", [this] { editSize(); });
    action(menu, "color", "Button color", [this] { editColor(); });
    action(menu, "themeColor", "Inherit theme color", [this, members] {
        for (const auto &id : members)
            service_.state.cards[id].color = QColor();
        service_.notify(true);
    });
    menu.addSeparator();
    action(menu, "exclusive", "Set mutually exclusive", [this] {
        createRelation();
    })->setEnabled(switchesOnly && members.size() >= 2);
    action(menu, "independent", "Make independent", [this, members] {
        service_.state.makeIndependent(members);
        service_.notify(true);
    });
    action(menu, "newGroup", "Create group", [this] { createGroup(); })->setEnabled(members.size() >= 2);
    auto *groups = menu.addMenu(text("addToGroup", "Move to group"));
    UI::bindLocalizedText(groups, "title", "signalSwitch", "addToGroup", "Move to group");
    for (auto it = service_.state.groups.begin(); it != service_.state.groups.end(); ++it)
    {
        auto *item = groups->addAction(it->name);
        connect(item, &QAction::triggered, &menu, [this, members, id = it.key()] {
            auto candidate = service_.state;
            auto target = candidate.groups[id];
            candidate.ungroup(members);
            for (const auto &member : members)
                if (!target.members.contains(member))
                    target.members.append(member);
            if (candidate.setGroup(id, target.name, target.members))
            {
                service_.state = std::move(candidate);
                service_.notify(true);
            }
        });
    }
    groups->setEnabled(!service_.state.groups.isEmpty());
    action(menu, "ungroup", "Remove from group", [this, members] {
        service_.state.ungroup(members);
        service_.notify(true);
    });
    menu.exec(global);
}

void Editor::showGroupMenu(const QString &id, const QPoint &global)
{
    if (!service_.state.groups.contains(id))
        return;
    QMenu menu(this);
    action(menu, "renameGroup", "Rename group", [this, id] {
        auto name = service_.state.groups[id].name;
        if (editText("renameGroup", "Rename group", &name))
        {
            service_.state.groups[id].name = name;
            service_.notify(true);
        }
    });
    action(menu, "selectGroup", "Select group members", [this, id] {
        const auto members = service_.state.groups[id].members;
        selected_ = QSet<QString>(members.begin(), members.end());
        refresh();
    });
    action(menu, "dissolveGroup", "Dissolve group", [this, id] {
        service_.state.groups.remove(id);
        service_.notify(true);
    });
    menu.exec(global);
}

void Editor::showCanvasMenu(const QPoint &global)
{
    QMenu menu(this);
    action(menu, "lock", "Lock layout", [this] { setLocked(true); });
    auto *grid = action(menu, "grid", "Snap to grid", [this] {
        service_.state.grid = !service_.state.grid;
        service_.notify(true);
    });
    grid->setCheckable(true);
    grid->setChecked(service_.state.grid);
    action(menu, "gridSize", "Grid spacing", [this] { editGrid(); });
    action(menu, "opacity", "Button opacity", [this] { editOpacity(); });
    auto *through = action(menu, "clickThrough", "Mouse click-through", [this] {
        service_.state.window.clickThrough = !service_.state.window.clickThrough;
        service_.notify(true);
    });
    through->setCheckable(true);
    through->setChecked(service_.state.window.clickThrough);
    action(menu, "closeEditor", "Close editor", [this] { close(); });
    menu.exec(global);
}

void Editor::editSize()
{
    if (selected_.isEmpty())
        return;
    QDialog dialog(this);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "size", "Width and height");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    const auto first = service_.state.cards.value(*selected_.begin()).geometry.size();
    auto addDimension = [&](const char *key, const char *english, bool width) {
        auto *field = new C::IntegerField(&dialog);
        field->setObjectName(QString("signalSwitch.") + key);
        field->setRange(C::sidebarControlHeight - 1, 1000000);
        UI::bindLocalizedText(field, "specialValueText", "signalSwitch", "multiple", "Multiple values");
        const int value = width ? first.width() : first.height();
        bool mixed = false;
        for (const auto &id : selected_)
        {
            const auto size = service_.state.cards.value(id).geometry.size();
            mixed |= (width ? size.width() : size.height()) != value;
        }
        field->setValue(mixed ? C::sidebarControlHeight - 1 : value);
        auto *label = new C::TextLabel(&dialog);
        UI::bindLocalizedText(label, "text", "signalSwitch", key, english);
        form->addRow(label, field);
        return field;
    };
    auto *width = addDimension("width", "Width", true);
    auto *height = addDimension("height", "Height", false);
    dialogButtons(dialog, layout);
    if (dialog.exec() != QDialog::Accepted)
        return;
    for (const auto &id : selected_)
    {
        auto &rect = service_.state.cards[id].geometry;
        if (width->value() >= C::sidebarControlHeight)
            rect.setWidth(width->value());
        if (height->value() >= C::sidebarControlHeight)
            rect.setHeight(height->value());
    }
    service_.notify(true);
}

void Editor::editColor()
{
    if (selected_.isEmpty())
        return;
    auto value = service_.state.cards.value(*selected_.begin()).color.name(QColor::HexArgb);
    if (!editText("colorPrompt", "Button color (#RRGGBB or #AARRGGBB)", &value))
        return;
    const QColor color(value);
    if (!color.isValid())
    {
        UI::bindLocalizedText(status_, "text", "signalSwitch", "invalidColor", "Enter a valid color value.");
        status_->show();
        return;
    }
    for (const auto &id : selected_)
        service_.state.cards[id].color = color;
    service_.notify(true);
}

void Editor::editGrid()
{
    QDialog dialog(this);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "gridSize", "Grid spacing");
    auto *layout = new QVBoxLayout(&dialog);
    auto *x = new C::IntegerField(&dialog);
    auto *y = new C::IntegerField(&dialog);
    x->setObjectName("signalSwitch.gridX");
    y->setObjectName("signalSwitch.gridY");
    x->setRange(1, 4096);
    y->setRange(1, 4096);
    x->setValue(service_.state.gridX);
    y->setValue(service_.state.gridY);
    auto *form = new QFormLayout;
    auto *horizontal = new C::TextLabel(&dialog);
    auto *vertical = new C::TextLabel(&dialog);
    UI::bindLocalizedText(horizontal, "text", "signalSwitch", "horizontal", "Horizontal");
    UI::bindLocalizedText(vertical, "text", "signalSwitch", "vertical", "Vertical");
    form->addRow(horizontal, x);
    form->addRow(vertical, y);
    layout->addLayout(form);
    dialogButtons(dialog, layout);
    if (dialog.exec() == QDialog::Accepted)
    {
        service_.state.gridX = x->value();
        service_.state.gridY = y->value();
        service_.notify(true);
    }
}

void Editor::editOpacity()
{
    QDialog dialog(this);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "opacity", "Control opacity");
    auto *layout = new QVBoxLayout(&dialog);
    auto *value = new C::IntegerField(&dialog);
    value->setObjectName("signalSwitch.opacity");
    value->setRange(10, 100);
    value->setSuffix("%");
    value->setValue(service_.state.window.opacity);
    layout->addWidget(value);
    dialogButtons(dialog, layout);
    if (dialog.exec() == QDialog::Accepted)
    {
        service_.state.window.opacity = value->value();
        service_.notify(true);
    }
}

void Editor::createGroup()
{
    QString name;
    if (!editText("newGroup", "Create group", &name))
        return;
    if (!service_.state.setGroup(freshId(), name, selected_.values()))
    {
        reportConflict();
        return;
    }
    int top = C::sidebarControlHeight;
    for (const auto &id : selected_)
        top = std::min(top, service_.state.cards[id].geometry.top());
    for (const auto &id : selected_)
        service_.state.cards[id].geometry.translate(0, C::sidebarControlHeight - top);
    service_.notify(true);
}

void Editor::createRelation()
{
    const auto members = selected_.values();
    auto candidate = service_.state;
    if (!candidate.setExclusive(freshId(), members))
    {
        reportConflict();
        return;
    }
    unsigned enabled{};
    for (const auto &id : members)
        enabled += service_.find(id) && service_.find(id)->desired;
    QString winner;
    if (enabled > 1)
    {
        QDialog dialog(this);
        UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "keepEnabled",
                              "Choose the switch to keep enabled");
        auto *layout = new QVBoxLayout(&dialog);
        auto *choice = new C::ChoiceField(&dialog);
        choice->setObjectName("signalSwitch.exclusiveWinner");
        choice->addItem(text("allOff", "All off"), QString());
        for (const auto &id : members)
            if (const auto *endpoint = service_.find(id))
                choice->addItem(service_.label(*endpoint), id);
        if (auto *manager = UI::I18nManager::installed())
            manager->bind(choice, [this, choice] {
                choice->setItemText(0, text("allOff", "All off"));
                for (int index = 1; index < choice->count(); ++index)
                    if (const auto *endpoint = service_.find(choice->itemData(index).toString()))
                        choice->setItemText(index, service_.label(*endpoint));
            });
        layout->addWidget(choice);
        dialogButtons(dialog, layout);
        if (dialog.exec() != QDialog::Accepted)
            return;
        winner = choice->currentData().toString();
    }
    service_.state = std::move(candidate);
    if (enabled > 1)
    {
        service_.setEnabled(members, false);
        if (!winner.isEmpty())
            service_.setEnabled({winner}, true);
    }
    service_.notify(true);
}
} // namespace WinJACKNexus::SignalSwitch
