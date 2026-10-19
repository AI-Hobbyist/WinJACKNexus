// SPDX-License-Identifier: AGPL-3.0-only
#include "ClassicMixerMessages.hpp"
#include <QDialog>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/ClassicMixerWindow.hpp>
#include <WinJACKNexus/UI/ConsoleControls.hpp>
#include <WinJACKNexus/UI/I18nManager.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>

namespace WinJACKNexus::UI
{
namespace C = Controls;
namespace CM = Common::ClassicMixer;
namespace
{
const QString scope = "module-winjacknexus-classic-mixer";
QString text(const char *key, const char *english)
{
    return localizedText("classicMixer", key, english);
}
void bind(QObject *object, const char *property, const char *key, const char *english)
{
    bindLocalizedText(object, property, "classicMixer", key, english);
}
constexpr int stripWidth = 200;
constexpr int monitorStripWidth = 152;
const std::array<int, 5> rowHeights{32, 96, 200, 124, 96};
} // namespace

struct ClassicMixerWindow::Impl
{
    ClassicMixerWindow &owner;
    State state;
    Commit commit;
    MeterReader meterReader;
    QString clientName;
    QString lastError;
    QTimer timer;
    QElapsedTimer elapsed;
    C::ScrollField *inputsScroll = nullptr;
    C::ChoiceField *presetChoice = nullptr;
    C::TextField *presetName = nullptr;
    C::ChoiceField *meterChoice = nullptr;
    C::TextLabel *layoutLabel = nullptr;
    C::TextLabel *status = nullptr;
    QWidget *stripArea = nullptr;
    std::vector<C::ConsoleFader *> faders;
    std::vector<std::function<void()>> valueBindings;
    bool synchronizing = false;

    QByteArray domain() const
    {
        return "classicMixer" + QByteArray::number(state.layout);
    }
    QString text(const char *key, const char *english) const
    {
        return localizedText(domain().constData(), key, english);
    }
    void bind(QObject *object, const char *property, const char *key, const char *english)
    {
        bindLocalizedText(object, property, domain().constData(), key, english);
    }

    Impl(ClassicMixerWindow &window, const State &initial, Commit handler, MeterReader reader)
        : owner(window), state(initial), commit(std::move(handler)), meterReader(std::move(reader))
    {
        owner.setObjectName("classicMixer.window");
        owner.setAttribute(Qt::WA_DeleteOnClose, false);
        owner.setMinimumSize(650, 450);
        owner.resize(1360, 940);
        auto *outer = new QVBoxLayout(&owner);
        auto *tools = new QHBoxLayout;
        presetChoice = new C::ChoiceField(&owner);
        presetChoice->setObjectName("classicMixer.presets");
        presetChoice->setMinimumWidth(150);
        presetName = new C::TextField(&owner);
        presetName->setObjectName("classicMixer.presetName");
        presetName->setMaxLength(128);
        presetName->setMinimumWidth(130);
        bind(presetName, "placeholderText", "preset.name", "Preset name");
        tools->addWidget(presetChoice);
        tools->addWidget(presetName);
        const auto action = [&](const char *id, const char *key, const char *english,
                                auto callback) {
            auto *button = new C::ActionButton({}, &owner);
            button->setObjectName(QString::fromUtf8(id));
            bind(button, "text", key, english);
            QObject::connect(button, &QPushButton::clicked, &owner, callback);
            tools->addWidget(button);
        };
        action("classicMixer.applyPreset", "preset.apply", "Apply", [this] {
            State candidate;
            QString error;
            if (state.applyPreset(presetChoice->currentData().toString(), candidate, &error))
                apply(candidate);
            else
                showError(error);
        });
        action("classicMixer.savePreset", "preset.save", "Save", [this] {
            auto candidate = state;
            if (candidate.savePreset(presetName->text(), presetChoice->currentData().toString()))
                apply(candidate);
            else
                showError("Enter a valid preset name.");
        });
        action("classicMixer.newPreset", "preset.new", "Save as", [this] {
            auto candidate = state;
            if (candidate.savePreset(presetName->text()))
                apply(candidate);
            else
                showError("Enter a valid preset name.");
        });
        action("classicMixer.deletePreset", "preset.delete", "Delete", [this] {
            auto candidate = state;
            const auto id = presetChoice->currentData().toString();
            std::erase_if(candidate.presets, [&](const auto &preset) { return preset.id == id; });
            candidate.selectedPreset.clear();
            apply(candidate);
        });
        QObject::connect(presetChoice, &QComboBox::currentIndexChanged, &owner, [this] {
            for (const auto &preset : state.presets)
            {
                if (preset.id == presetChoice->currentData().toString())
                    presetName->setText(preset.name);
            }
        });
        layoutLabel = new C::TextLabel(&owner);
        tools->addWidget(layoutLabel);
        meterChoice = new C::ChoiceField(&owner);
        meterChoice->setObjectName("classicMixer.meterPoint");
        meterChoice->setMinimumWidth(150);
        tools->addWidget(meterChoice);
        QObject::connect(meterChoice, &QComboBox::currentIndexChanged, &owner, [this] {
            if (!synchronizing)
                mutate([this](State &candidate) {
                    candidate.postMeter = meterChoice->currentData().toBool();
                });
        });
        outer->addLayout(tools);
        auto *verticalScroll = new C::ScrollField(&owner);
        verticalScroll->setObjectName("classicMixer.verticalScroll");
        verticalScroll->setFrameShape(QFrame::NoFrame);
        verticalScroll->setWidgetResizable(true);
        verticalScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        stripArea = new QWidget;
        auto *strips = new QHBoxLayout(stripArea);
        strips->setContentsMargins(0, 0, 0, 0);
        strips->setSpacing(0);
        inputsScroll = new C::ScrollField(stripArea);
        inputsScroll->setObjectName("classicMixer.inputScroll");
        inputsScroll->setFrameShape(QFrame::NoFrame);
        inputsScroll->setWidgetResizable(true);
        inputsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        inputsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        inputsScroll->setMinimumWidth(stripWidth + 6);
        auto *inputs = new QWidget;
        auto *inputLayout = new QHBoxLayout(inputs);
        inputLayout->setContentsMargins(0, 0, 0, 0);
        inputLayout->setSpacing(0);
        for (std::size_t index = 0; index < state.inputs.size(); ++index)
        {
            if (index)
                inputLayout->addWidget(separator(inputs));
            inputLayout->addWidget(makeStrip(static_cast<int>(index), inputs));
        }
        inputsScroll->setWidget(inputs);
        strips->addWidget(inputsScroll, 1);
        strips->addWidget(separator(stripArea));
        // Reserve the same scrollbar band below fixed strips to keep faders aligned.
        for (int index = 0; index < 2; ++index)
        {
            auto *fixed = new QWidget(stripArea);
            auto *layout = new QVBoxLayout(fixed);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            layout->addWidget(makeStrip(static_cast<int>(state.inputs.size()) + index, fixed), 1);
            auto *scrollbarSpace = new QWidget(fixed);
            scrollbarSpace->setFixedHeight(
                inputsScroll->horizontalScrollBar()->sizeHint().height());
            layout->addWidget(scrollbarSpace);
            fixed->setFixedWidth(index == 0 ? stripWidth : monitorStripWidth);
            strips->addWidget(fixed);
            if (!index)
                strips->addWidget(separator(stripArea));
        }
        verticalScroll->setWidget(stripArea);
        outer->addWidget(verticalScroll, 1);
        status = new C::TextLabel(&owner);
        status->setObjectName("classicMixer.status");
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        outer->addWidget(status);
        bindRefreshTimer(timer, &owner);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] {
            const double seconds = elapsed.isValid() ? elapsed.restart() / 1000.0 : 0;
            if (!elapsed.isValid())
                elapsed.start();
            const auto levels = meterReader ? meterReader() : CM::Levels{};
            for (std::size_t index = 0; index < faders.size(); ++index)
                faders[index]->setLevels(state.postMeter ? levels.post[index] : levels.input[index],
                                         seconds,
                                         ApplicationSettings::instance().peakHoldSeconds());
        });
        if (auto *manager = I18nManager::installed())
            manager->bind(&owner, [this] { retranslate(); });
        else
            retranslate();
        refreshValues();
        C::bindConsoleTheme(&owner, scope);
    }

    C::ConsoleSeparator *separator(QWidget *parent)
    {
        auto *divider = new C::ConsoleSeparator(scope, parent);
        divider->setObjectName("classicMixer.channelDivider");
        return divider;
    }
    void showError(const QString &error)
    {
        lastError = error;
        status->setText(text("error.apply", "Unable to apply mixer settings.") + ' ' +
                        classicMixerDiagnostic(error, domain().constData()));
    }
    bool apply(const State &candidate)
    {
        QString error;
        if (commit && !commit(candidate, &error))
        {
            showError(error);
            refreshValues();
            return false;
        }
        state = candidate;
        status->clear();
        lastError.clear();
        refreshValues();
        return true;
    }
    void mutate(const std::function<void(State &)> &change)
    {
        if (synchronizing)
            return;
        auto candidate = state;
        change(candidate);
        apply(candidate);
    }
    void refreshValues()
    {
        synchronizing = true;
        for (const auto &binding : valueBindings)
            binding();
        {
            const QSignalBlocker blocker(meterChoice);
            meterChoice->setCurrentIndex(state.postMeter ? 1 : 0);
        }
        const auto selected = presetChoice->currentData().toString();
        {
            const QSignalBlocker blocker(presetChoice);
            presetChoice->clear();
            presetChoice->addItem(text("preset.none", "No preset"), QString());
            for (const auto &preset : state.presets)
                presetChoice->addItem(preset.name, preset.id);
            const auto id = state.selectedPreset.isEmpty() ? selected : state.selectedPreset;
            presetChoice->setCurrentIndex(std::max(0, presetChoice->findData(id)));
        }
        synchronizing = false;
    }
    void retranslate()
    {
        owner.setWindowTitle(text("name", "Classic Mixer") +
                             (clientName.isEmpty() ? QString() : " — " + clientName));
        layoutLabel->setText(text("layout.label", "Layout") + ": " + QString::number(state.layout));
        const QSignalBlocker blocker(meterChoice);
        meterChoice->clear();
        meterChoice->addItem(text("meter.input", "Input"), false);
        meterChoice->addItem(text("meter.post", "Post fader"), true);
        meterChoice->setCurrentIndex(state.postMeter ? 1 : 0);
        if (presetChoice->count())
            presetChoice->setItemText(0, text("preset.none", "No preset"));
        if (!lastError.isEmpty())
            status->setText(text("error.apply", "Unable to apply mixer settings.") + ' ' +
                            classicMixerDiagnostic(lastError, domain().constData()));
    }

    QWidget *slot(QWidget *strip, int row, bool card)
    {
        QWidget *widget = card ? new C::GroupPanel({}, strip) : new QWidget(strip);
        widget->setFixedHeight(rowHeights[row]);
        return widget;
    }
    C::TextLabel *label(const char *key, const char *english, QWidget *parent)
    {
        auto *result = new C::TextLabel(parent);
        result->setAlignment(Qt::AlignCenter);
        bind(result, "text", key, english);
        return result;
    }
    using ReadNumber = std::function<double(const State &)>;
    using WriteNumber = std::function<void(State &, double)>;
    C::ConsoleKnob *knob(QWidget *parent, QGridLayout *layout, int row, int column,
                         const QString &id, const char *key, const char *english,
                         C::NumericControlSpec spec, ReadNumber read, WriteNumber write,
                         bool logarithmic = false)
    {
        auto *box = new QWidget(parent);
        auto *stack = new QVBoxLayout(box);
        stack->setContentsMargins(0, 0, 0, 0);
        stack->setSpacing(0);
        stack->addWidget(label(key, english, box));
        auto *control = new C::ConsoleKnob(scope, spec, logarithmic, box);
        control->setObjectName(id);
        control->setUnit(logarithmic ? " Hz" : (spec.minimum == -1 ? QString() : " dB"));
        bind(control, "accessibleName", key, english);
        control->onChanged([this, write](double value) {
            mutate([&](State &candidate) { write(candidate, value); });
        });
        valueBindings.push_back([this, control, read] { control->setValue(read(state)); });
        stack->addWidget(control, 0, Qt::AlignHCenter);
        layout->addWidget(box, row, column);
        return control;
    }
    using ReadBool = std::function<bool(const State &)>;
    using WriteBool = std::function<void(State &, bool)>;
    C::ActionButton *toggle(QWidget *parent, const QString &id, const char *key,
                            const char *english, ReadBool read, WriteBool write)
    {
        auto *button = new C::ActionButton({}, parent);
        button->setCheckable(true);
        button->setObjectName(id);
        bind(button, "text", key, english);
        bind(button, "accessibleName", key, english);
        QObject::connect(button, &QPushButton::toggled, &owner, [this, write](bool on) {
            mutate([&](State &candidate) { write(candidate, on); });
        });
        valueBindings.push_back([this, button, read] {
            const QSignalBlocker blocker(button);
            button->setChecked(read(state));
        });
        return button;
    }
    QWidget *makeStrip(int index, QWidget *parent)
    {
        const int count = static_cast<int>(state.inputs.size());
        const bool input = index < count;
        const bool master = index == count;
        const auto prefix = QStringLiteral("classicMixer.strip.%1.").arg(index);
        auto *strip = new QWidget(parent);
        strip->setObjectName(prefix + "panel");
        strip->setFixedWidth(input || master ? stripWidth : monitorStripWidth);
        auto *stack = new QVBoxLayout(strip);
        stack->setContentsMargins(4, 0, 4, 0);
        stack->setSpacing(0);
        std::array<QWidget *, 5> rows{};
        const char *sections[]{"title", "gain", "eq", "aux", "pan"};
        for (int row = 0; row < 5; ++row)
        {
            rows[row] = slot(strip, row, (input && row > 0) || (master && (row == 2 || row == 3)));
            rows[row]->setObjectName(prefix + sections[row] + "Section");
            stack->addWidget(rows[row]);
        }
        auto *titleLayout = new QVBoxLayout(rows[0]);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        if (input)
        {
            auto *name = new C::TextField(rows[0]);
            name->setObjectName(prefix + "name");
            name->setMaxLength(96);
            name->setAlignment(Qt::AlignCenter);
            bind(name, "accessibleName", "channel.name", "Channel name");
            titleLayout->addWidget(name);
            valueBindings.push_back([this, name, index] {
                if (!name->hasFocus())
                    name->setText(state.inputs[index].name);
            });
            QObject::connect(name, &QLineEdit::editingFinished, &owner, [this, name, index] {
                mutate([&](State &candidate) {
                    candidate.inputs[index].name = name->text().trimmed();
                });
                name->setText(state.inputs[index].name);
            });
        }
        else
            titleLayout->addWidget(
                label(master ? "master" : "monitor", master ? "Master" : "Monitor", rows[0]));
        if (input)
        {
            auto *gain = new QGridLayout(rows[1]);
            gain->setContentsMargins(4, 12, 4, 4);
            knob(
                rows[1], gain, 0, 0, prefix + "gain", "gain", "Gain", {-12, 12, 0, 1},
                [index](const State &s) { return s.inputs[index].gain; },
                [index](State &s, double value) { s.inputs[index].gain = value; });
        }
        if (input || master)
        {
            auto *eqLayout = new QGridLayout(rows[2]);
            eqLayout->setContentsMargins(4, 12, 4, 4);
            eqLayout->setSpacing(0);
            const auto getEq = [index, input](State &s) -> CM::Equalizer & {
                return input ? s.inputs[index].eq : s.masterEq;
            };
            const auto readEq = [index, input](const State &s) -> const CM::Equalizer & {
                return input ? s.inputs[index].eq : s.masterEq;
            };
            eqLayout->addWidget(toggle(
                                    rows[2], prefix + "eqEnabled", "eq.enabled", "EQ",
                                    [readEq](const State &s) { return readEq(s).enabled; },
                                    [getEq](State &s, bool on) { getEq(s).enabled = on; }),
                                0, 0, 1, 2);
            const char *keys[]{"eq.low", "eq.mid", "eq.high", "eq.frequency"};
            const char *english[]{"Low", "Mid", "High", "Mid Hz"};
            for (int band = 0; band < 4; ++band)
            {
                const auto read = [readEq, band](const State &s) {
                    const auto &eq = readEq(s);
                    return band == 0   ? eq.low
                           : band == 1 ? eq.mid
                           : band == 2 ? eq.high
                                       : eq.frequency;
                };
                const auto write = [getEq, band](State &s, double value) {
                    auto &eq = getEq(s);
                    if (band == 0)
                        eq.low = value;
                    else if (band == 1)
                        eq.mid = value;
                    else if (band == 2)
                        eq.high = value;
                    else
                        eq.frequency = value;
                };
                knob(rows[2], eqLayout, 1 + band / 2, band % 2, prefix + keys[band], keys[band],
                     english[band],
                     band == 3 ? C::NumericControlSpec{100, 8000, 1000, 0}
                               : C::NumericControlSpec{-12, 12, 0, 1},
                     read, write, band == 3);
            }
            auto *auxLayout = new QGridLayout(rows[3]);
            auxLayout->setContentsMargins(4, 12, 4, 4);
            auxLayout->setSpacing(0);
            for (int bus = 0; bus < 2; ++bus)
            {
                knob(
                    rows[3], auxLayout, 0, bus, prefix + QStringLiteral("aux%1").arg(bus),
                    bus ? "aux.2" : "aux.1", bus ? "AUX 2" : "AUX 1",
                    {CM::silenceDb, 12, input ? CM::silenceDb : 0, 1},
                    [input, index, bus](const State &s) {
                        return input ? s.inputs[index].sends[bus] : s.auxMaster[bus];
                    },
                    [input, index, bus](State &s, double value) {
                        if (input)
                            s.inputs[index].sends[bus] = value;
                        else
                            s.auxMaster[bus] = value;
                    });
                if (input)
                {
                    auto *choice = new C::ChoiceField(rows[3]);
                    choice->setObjectName(prefix + QStringLiteral("pre%1").arg(bus));
                    const auto language = [this, choice] {
                        const auto selected = choice->currentIndex();
                        const QSignalBlocker blocker(choice);
                        choice->clear();
                        choice->addItem(text("aux.post", "Post"), false);
                        choice->addItem(text("aux.pre", "Pre"), true);
                        choice->setCurrentIndex(std::max(0, selected));
                    };
                    if (auto *manager = I18nManager::installed())
                        manager->bind(choice, language);
                    else
                        language();
                    auxLayout->addWidget(choice, 1, bus);
                    valueBindings.push_back([this, choice, index, bus] {
                        const QSignalBlocker blocker(choice);
                        choice->setCurrentIndex(state.inputs[index].pre[bus] ? 1 : 0);
                    });
                    QObject::connect(choice, &QComboBox::currentIndexChanged, &owner,
                                     [this, choice, index, bus] {
                                         mutate([&](State &s) {
                                             s.inputs[index].pre[bus] =
                                                 choice->currentData().toBool();
                                         });
                                     });
                }
            }
        }
        auto *faderPanel = new C::GroupPanel({}, strip);
        faderPanel->setObjectName(prefix + "faderSection");
        auto *faderLayout = new QVBoxLayout(faderPanel);
        faderLayout->setContentsMargins(4, 12, 4, 4);
        auto *fader = new C::ConsoleFader(scope,
                                          input    ? "input"
                                          : master ? "master"
                                                   : "monitor",
                                          input && !state.inputs[index].stereo ? 1 : 2, faderPanel);
        fader->setObjectName(prefix + "fader");
        bind(fader, "accessibleName", "fader", "Fader");
        valueBindings.push_back([this, fader, index, input, master] {
            fader->setValue(input    ? state.inputs[index].fader
                            : master ? state.masterFader
                                     : state.monitorFader);
        });
        fader->onChanged([this, index, input, master](double value) {
            mutate([&](State &s) {
                if (input)
                    s.inputs[index].fader = value;
                else if (master)
                    s.masterFader = value;
                else
                    s.monitorFader = value;
            });
        });
        fader->setMinimumHeight(272);
        faderLayout->addWidget(fader);
        if (input)
        {
            auto *panLayout = new QGridLayout(rows[4]);
            panLayout->setContentsMargins(4, 12, 4, 4);
            auto *pan = knob(
                rows[4], panLayout, 0, 0, prefix + "pan", "pan", "Pan", {-1, 1, 0, 2},
                [index](const State &s) { return s.inputs[index].pan; },
                [index](State &s, double value) { s.inputs[index].pan = value; });
            pan->setPanMode(true);
            auto *buttons = new QVBoxLayout(fader);
            buttons->setContentsMargins(124, 16, 0, 36);
            buttons->setSpacing(4);
            const char *keys[]{"mix", "afl", "pfl", "a1", "a2", "mute", "solo"};
            const char *english[]{"MIX", "AFL", "PFL", "A1", "A2", "M", "S"};
            const char *help[]{"help.mix", "help.afl",  "help.pfl", "help.a1",
                               "help.a2",  "help.mute", "help.solo"};
            const char *helpEnglish[]{"Route to Master",
                                      "Listen after Gain, before EQ and fader",
                                      "Listen before Gain",
                                      "Enable AUX 1",
                                      "Enable AUX 2",
                                      "Mute Master and AUX contribution",
                                      "Solo this channel in Master"};
            for (int button = 0; button < 7; ++button)
            {
                const auto read = [index, button](const State &s) {
                    const auto &c = s.inputs[index];
                    switch (button)
                    {
                    case 0:
                        return c.mix;
                    case 1:
                        return c.listen == CM::Listen::afl;
                    case 2:
                        return c.listen == CM::Listen::pfl;
                    case 3:
                        return c.enabled[0];
                    case 4:
                        return c.enabled[1];
                    case 5:
                        return c.mute;
                    default:
                        return c.solo;
                    }
                };
                const auto write = [index, button](State &s, bool on) {
                    auto &c = s.inputs[index];
                    switch (button)
                    {
                    case 0:
                        c.mix = on;
                        break;
                    case 1:
                        c.listen = on ? CM::Listen::afl : CM::Listen::off;
                        break;
                    case 2:
                        c.listen = on ? CM::Listen::pfl : CM::Listen::off;
                        break;
                    case 3:
                        c.enabled[0] = on;
                        break;
                    case 4:
                        c.enabled[1] = on;
                        break;
                    case 5:
                        c.mute = on;
                        break;
                    default:
                        c.solo = on;
                        break;
                    }
                };
                auto *control = toggle(fader, prefix + keys[button], keys[button], english[button],
                                       read, write);
                bind(control, "toolTip", help[button], helpEnglish[button]);
                bind(control, "accessibleName", help[button], helpEnglish[button]);
                control->setFixedSize(44, 28);
                buttons->addWidget(control);
            }
            buttons->addStretch();
        }
        else if (master)
        {
            auto *buttons = new QVBoxLayout(fader);
            buttons->setContentsMargins(124, 16, 0, 36);
            auto *mute = toggle(
                fader, prefix + "mute", "mute", "M", [](const State &s) { return s.masterMute; },
                [](State &s, bool on) { s.masterMute = on; });
            bind(mute, "toolTip", "help.masterMute", "Mute Master output");
            mute->setFixedSize(44, 28);
            buttons->addWidget(mute);
            buttons->addStretch();
        }
        stack->addWidget(faderPanel, 1);
        faders.push_back(fader);
        return strip;
    }
};

ClassicMixerWindow::ClassicMixerWindow(const State &state, Commit commit, MeterReader reader)
    : impl_(std::make_unique<Impl>(*this, state, std::move(commit), std::move(reader)))
{
}
ClassicMixerWindow::~ClassicMixerWindow() = default;
void ClassicMixerWindow::setState(const State &state)
{
    if (state.layout == impl_->state.layout)
    {
        impl_->state = state;
        impl_->refreshValues();
    }
}
void ClassicMixerWindow::setClientName(const QString &name)
{
    impl_->clientName = name;
    impl_->retranslate();
}
void ClassicMixerWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    impl_->elapsed.start();
    impl_->timer.start();
}
void ClassicMixerWindow::hideEvent(QHideEvent *event)
{
    impl_->timer.stop();
    QWidget::hideEvent(event);
}
bool ClassicMixerWindow::chooseLayout(State &state, QWidget *parent)
{
    if (state.layoutChosen)
        return true;
    QDialog dialog(parent);
    dialog.setObjectName("classicMixer.chooseLayout");
    bind(&dialog, "windowTitle", "layout.choose", "Choose mixer layout");
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new C::TextLabel(
        text("layout.description", "Input audio channels (Master and Monitor are additional)"),
        &dialog));
    auto *choice = new C::ChoiceField(&dialog);
    for (int value : {8, 16, 24})
        choice->addItem(QString::number(value), value);
    choice->setCurrentIndex(choice->findData(state.layout));
    layout->addWidget(choice);
    auto *buttons =
        new C::DialogButtonBar(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    C::bindConsoleTheme(&dialog, scope);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    state = State::create(choice->currentData().toInt());
    state.layoutChosen = true;
    return true;
}
} // namespace WinJACKNexus::UI
