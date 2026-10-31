/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Editor.hpp"
#include "Sound.hpp"
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <cmath>

namespace WinJACKNexus::ControlDeck
{
namespace C = UI::Controls;
namespace
{
void buttons(QDialog &dialog, QVBoxLayout *layout)
{
    auto *bar = new C::DialogButtonBar(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(bar);
    QObject::connect(bar, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(bar, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
}

C::DecimalField *number(QDialog &dialog, QFormLayout *form, const char *key, const char *english, double low,
                        double high, double value)
{
    auto *field = new C::DecimalField(&dialog);
    field->setObjectName(QString("controlDeck.") + key);
    field->setRange(low, high);
    field->setDecimals(2);
    field->setSuffix(" dB");
    field->setValue(value);
    auto *label = new C::TextLabel(&dialog);
    UI::bindLocalizedText(label, "text", "signalSwitch", key, english);
    form->addRow(label, field);
    return field;
}
} // namespace

bool Editor::refreshControl(C::ActionButton *card, const Endpoint &endpoint)
{
    if (!endpoint.peak && !endpoint.gainLinear)
        return false;
    card->setText({});
    auto *title = card->findChild<QLabel *>("controlDeck.controlTitle");
    if (!title)
    {
        auto *layout = new QVBoxLayout(card);
        layout->setSizeConstraint(QLayout::SetNoConstraint);
        title = new C::TextLabel(card);
        title->setObjectName("controlDeck.controlTitle");
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(title);
        QWidget *control;
        if (endpoint.peak)
        {
            auto *meter = new UI::FxLevelMeterWidget(card);
            meter->setSurfaceVisible(false);
            meter->setProperty("midiToolsPassiveSurface", true);
            control = meter;
            control->setObjectName("controlDeck.peakMeter");
        }
        else
        {
            title->setAlignment(Qt::AlignCenter);
            title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            layout->setAlignment(Qt::AlignCenter);
            auto *knob =
                new C::ConsoleKnob("signal-switch", {endpoint.gainMinimum, endpoint.gainMaximum, 0, 2}, false, card);
            knob->setObjectName("controlDeck.gainKnob");
            knob->setProperty("midiToolsPassiveSurface", true);
            knob->setAutoFillBackground(false);
            knob->setAttribute(Qt::WA_NoSystemBackground);
            knob->setAttribute(Qt::WA_StyledBackground, false);
            knob->setUnit("dB");
            knob->setMinimumIsSilence(false);
            knob->onChanged([this, id = endpoint.id](double value) { service_.setGain(id, value); });
            control = knob;
            if (auto *current = service_.find(endpoint.id))
                current->editValue = [owner = QPointer<Editor>(this), id = endpoint.id] {
                    if (owner)
                        owner->editGainValue(id);
                };
        }
        control->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(control, &QWidget::customContextMenuRequested, this, [this, card, id = endpoint.id](QPoint) {
            if (!service_.state.locked)
                showCardMenu(id, card->mapToGlobal(card->rect().center()));
        });
        layout->addWidget(control, 1, endpoint.peak ? Qt::Alignment() : Qt::AlignHCenter);
    }
    const auto name = service_.label(endpoint);
    title->setText(name);
    card->setAccessibleName(name);
    if (endpoint.peak)
    {
        auto *meter = dynamic_cast<UI::FxLevelMeterWidget *>(card->findChild<QWidget *>("controlDeck.peakMeter"));
        const float amplitude = endpoint.peak->exchange(0, std::memory_order_relaxed);
        const double db = amplitude > 0 ? 20.0 * std::log10(double(amplitude)) : -72;
        meter->setSingleChannel(true, UI::localizedText("signalSwitch", "peakLevel", "Peak"));
        meter->setVertical(endpoint.peakVertical);
        meter->setLevels(db, db);
        card->setToolTip(name + "\n" + QString::number(std::clamp(db, -72.0, 12.0), 'f', 1) + " dB");
    }
    else
    {
        auto *knob = dynamic_cast<C::ConsoleKnob *>(card->findChild<QWidget *>("controlDeck.gainKnob"));
        knob->setRange(endpoint.gainMinimum, endpoint.gainMaximum);
        knob->setValue(endpoint.gainDb);
        knob->setAccessibleName(name);
        card->setToolTip(name + "\n" + QString::number(endpoint.gainDb, 'f', 2) + " dB");
    }
    return true;
}

void Editor::loadSound(const QString &id)
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::ExistingFile);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "loadSound", "Load sound…");
    // No extension whitelist: BASS and its loaded plugins identify file contents.
    dialog.setNameFilter(UI::localizedText("signalSwitch", "allFiles", "All files (*)"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    if (auto *endpoint = service_.find(id); endpoint && endpoint->sound)
    {
        if (endpoint->sound->load(dialog.selectedFiles().first()))
            service_.notify();
    }
}

void Editor::editShortcut(const QString &id)
{
    const auto *endpoint = service_.find(id);
    if (!endpoint || !endpoint->sound)
        return;
    QDialog dialog(this);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "shortcut", "Global shortcut…");
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new C::TextLabel(&dialog);
    UI::bindLocalizedText(hint, "text", "signalSwitch", "shortcutHint", "Example: Ctrl+Alt+1. Leave empty to clear.");
    layout->addWidget(hint);
    auto *field = new C::TextField(&dialog);
    field->setObjectName("controlDeck.shortcut");
    field->setText(endpoint->shortcut);
    layout->addWidget(field);
    auto *errorLabel = new C::TextLabel(&dialog);
    layout->addWidget(errorLabel);
    buttons(dialog, layout);
    // Keep the editor open on a conflict and preserve the previous registration.
    const auto bar = dialog.findChild<QDialogButtonBox *>();
    disconnect(bar, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bar, &QDialogButtonBox::accepted, &dialog, [&] {
        QString error;
        if (service_.setShortcut(id, field->text(), &error))
            dialog.accept();
        else
            UI::bindLocalizedText(errorLabel, "text", "signalSwitch", "shortcutConflict",
                                  "Invalid, reserved or already used shortcut. Choose another combination.");
    });
    dialog.exec();
}

void Editor::editGainRange(const QString &id)
{
    const auto *endpoint = service_.find(id);
    if (!endpoint || !endpoint->gainLinear)
        return;
    QDialog dialog(this);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "gainRange", "Gain range…");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *minimum = number(dialog, form, "minimum", "Minimum", -720, 720, endpoint->gainMinimum);
    auto *maximum = number(dialog, form, "maximum", "Maximum", -720, 720, endpoint->gainMaximum);
    auto *errorLabel = new C::TextLabel(&dialog);
    layout->addWidget(errorLabel);
    buttons(dialog, layout);
    const auto bar = dialog.findChild<QDialogButtonBox *>();
    disconnect(bar, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bar, &QDialogButtonBox::accepted, &dialog, [&] {
        if (service_.setGainRange(id, minimum->value(), maximum->value()))
            dialog.accept();
        else
            UI::bindLocalizedText(errorLabel, "text", "signalSwitch", "invalidRange",
                                  "Minimum must be less than maximum.");
    });
    dialog.exec();
}

void Editor::editGainValue(const QString &id)
{
    const auto *endpoint = service_.find(id);
    if (!endpoint || !endpoint->gainLinear)
        return;
    QDialog dialog(this);
    UI::bindLocalizedText(&dialog, "windowTitle", "signalSwitch", "gainValue", "Gain");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *value =
        number(dialog, form, "gainValue", "Gain", endpoint->gainMinimum, endpoint->gainMaximum, endpoint->gainDb);
    buttons(dialog, layout);
    if (dialog.exec() == QDialog::Accepted)
        service_.setGain(id, value->value());
}
} // namespace WinJACKNexus::ControlDeck
