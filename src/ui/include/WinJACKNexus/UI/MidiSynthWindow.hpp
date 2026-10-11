// Composition of public controls and drawing APIs; visuals follow shared theme tokens.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHideEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <WinJACKNexus/Common/MidiToolsSynth.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/MidiSynthDrawControls.hpp>
#include <WinJACKNexus/UI/MidiToolsTheme.hpp>
#include <cmath>
#include <functional>

namespace WinJACKNexus::UI
{
struct MidiSynthBindings
{
    using Settings = Common::MidiTools::SynthSettings;
    std::function<bool(const Settings &, bool)> edit;
    std::function<Common::MidiTools::SynthStatus()> status;
    std::function<std::vector<Common::MidiTools::ResourceInfo>()> resources;
    std::function<bool()> loading;
    std::function<QString(const char *, const char *)> text;
};

class MidiSynthWindow final : public QWidget
{
    using Settings = Common::MidiTools::SynthSettings;
    using Resource = Common::MidiTools::ResourceSpec;
    using ResourceState = Common::MidiTools::ResourceState;
    MidiSynthBindings bindings_;
    Settings settings_;
    Common::MidiTools::SynthStatus status_;
    std::vector<Common::MidiTools::ResourceInfo> information_;
    Controls::TabField *tabs_{};
    std::array<Controls::ListField *, 2> lists_{};
    Controls::ActionButton *import_{}, *remove_{}, *prefer_{};
    Controls::TextLabel *details_{}, *message_{}, *mappingLabel_{}, *diagnostics_{};
    std::array<Controls::IntegerField *, 3> mapping_{};
    std::array<Controls::TextLabel *, 5> headings_{};
    std::array<Controls::TextLabel *, 16> instruments_{};
    std::array<MidiSynthGain *, 16> gains_{};
    std::array<Controls::ActionButton *, 16> mutes_{}, solos_{};
    std::array<MidiSynthMeter *, 18> meters_{};
    std::array<Controls::TextLabel *, 18> levels_{};
    std::array<Controls::MeterBallistics, 18> ballistics_{};
    Controls::GroupPanel *master_{};
    MidiSynthGain *masterGain_{};
    Controls::ActionButton *masterMute_{};
    QTimer refresh_{this};
    QElapsedTimer clock_;
    bool wasLoading_ = true;
    std::string selectedId_;

    QString text(const char *key, const char *fallback) const
    {
        return bindings_.text(key, fallback);
    }
    QString stateName(ResourceState state) const
    {
        switch (state)
        {
        case ResourceState::ready:
            return text("ready", "Ready");
        case ResourceState::missing:
            return text("missing", "Missing file");
        case ResourceState::failed:
            return text("failed", "Load failed");
        case ResourceState::budget:
            return text("budget", "Resource budget exceeded");
        default:
            return text("pending", "Preparing");
        }
    }
    const Resource *selected() const
    {
        for (const auto &resource : settings_.resources)
        {
            if (resource.id == selectedId_)
            {
                return &resource;
            }
        }
        return nullptr;
    }
    MidiSynthGain *gainField(const QString &name, QWidget *parent, bool master = false)
    {
        auto *field = new MidiSynthGain(parent, master);
        field->setObjectName(name);
        return field;
    }
    Controls::ActionButton *switchButton(const QString &name, QWidget *parent)
    {
        auto *button = new Controls::ActionButton({}, parent);
        button->setObjectName(name);
        button->setCheckable(true);
        return button;
    }
    void addMeter(QGridLayout *layout, unsigned index, int row, int column, QWidget *parent)
    {
        meters_[index] = new MidiSynthMeter(parent);
        meters_[index]->setObjectName(QStringLiteral("synth-level-%1").arg(index));
        levels_[index] = new Controls::TextLabel(QStringLiteral("−∞ dB"), parent);
        levels_[index]->setMinimumWidth(fontMetrics().horizontalAdvance("-72.0 dB") + 12);
        layout->addWidget(meters_[index], row, column);
        layout->addWidget(levels_[index], row, column + 1);
    }
    void refreshLists()
    {
        for (unsigned format = 0; format < 2; ++format)
        {
            QSignalBlocker blocker(lists_[format]);
            lists_[format]->clear();
            for (const auto &resource : settings_.resources)
            {
                if (resource.format != (format == 0 ? "sf2" : "sfz"))
                {
                    continue;
                }
                ResourceState state = ResourceState::pending;
                for (const auto &info : information_)
                {
                    if (info.spec.id == resource.id)
                    {
                        state = info.state;
                    }
                }
                const auto path = QString::fromStdString(resource.path);
                auto *item = new QListWidgetItem(
                    QFileInfo(path).fileName() + " · " + stateName(state), lists_[format]);
                item->setData(Qt::UserRole, QString::fromStdString(resource.id));
                item->setToolTip(path);
                if (resource.id == selectedId_)
                {
                    lists_[format]->setCurrentItem(item);
                }
            }
        }
        refreshDetails();
    }
    void refreshDetails()
    {
        const auto *resource = selected();
        remove_->setEnabled(resource != nullptr);
        prefer_->setEnabled(resource != nullptr);
        const bool sfz = resource && resource->format == "sfz";
        for (unsigned index = 0; index < 3; ++index)
        {
            QSignalBlocker blocker(mapping_[index]);
            mapping_[index]->setEnabled(sfz);
            mapping_[index]->setValue(sfz ? (index == 0   ? resource->msb
                                             : index == 1 ? resource->lsb
                                                          : resource->program)
                                          : 0);
        }
        if (!resource)
        {
            details_->setText(text(
                "browseHelp", "Select a file to inspect it. Browsing does not change playback."));
            return;
        }
        QString state = stateName(ResourceState::pending);
        QString detail;
        size_t presets = 0;
        for (const auto &info : information_)
        {
            if (info.spec.id == resource->id)
            {
                state = stateName(info.state);
                detail = QString::fromStdString(info.detail);
                presets = info.presets.size();
            }
        }
        details_->setText(
            QString::fromStdString(resource->path) + "\n" +
            QString::fromStdString(resource->format).toUpper() + " · " + state + "\n" +
            text("presets", "Presets") + ": " + QString::number(presets) + "\n" +
            text("preferred", "Preferred resource") + ": " +
            (settings_.preferred == resource->id ? text("yes", "Yes") : text("no", "No")) +
            (detail.isEmpty() ? QString() : "\n" + detail));
    }
    bool commit(Settings candidate, bool preserve = false)
    {
        if (!bindings_.edit(candidate, preserve))
        {
            message_->setText(text("rejected", "Invalid or duplicate resource/settings."));
            setSettings(settings_);
            return false;
        }
        setSettings(candidate);
        message_->setText(bindings_.loading() ? text("pending", "Preparing") : QString());
        return true;
    }
    void changeMapping()
    {
        auto candidate = settings_;
        for (auto &resource : candidate.resources)
        {
            if (resource.id == selectedId_ && resource.format == "sfz")
            {
                resource.msb = uint8_t(mapping_[0]->value());
                resource.lsb = uint8_t(mapping_[1]->value());
                resource.program = uint8_t(mapping_[2]->value());
                commit(std::move(candidate), true);
                return;
            }
        }
    }
    void refreshAudio()
    {
        status_ = bindings_.status();
        const double elapsed = clock_.isValid() ? clock_.restart() / 1000.0
                                                : 1.0 / ApplicationSettings::instance().frameRate();
        if (!clock_.isValid())
        {
            clock_.start();
        }
        for (unsigned index = 0; index < 18; ++index)
        {
            const float peak = index < 16    ? status_.channels[index].peak
                               : index == 16 ? status_.leftPeak
                                             : status_.rightPeak;
            const double db = peak > 0 ? 20.0 * std::log10(peak) : -72.0;
            ballistics_[index].advance(db, elapsed,
                                       ApplicationSettings::instance().peakHoldSeconds());
            meters_[index]->setLevels(ballistics_[index].levelDb(), ballistics_[index].peakDb());
            levels_[index]->setText(peak > 0 ? QString::number(db, 'f', 1) + " dB"
                                             : QStringLiteral("−∞ dB"));
            levels_[index]->setToolTip(db > 0 ? text("overload", "Overload") : QString());
            if (index < 16)
            {
                const auto &channel = status_.channels[index];
                instruments_[index]->setText(channel.matched && !status_.preparing
                                                 ? QString::fromUtf8(channel.preset.data())
                                                 : text("unmatched", "No matching instrument"));
                instruments_[index]->setToolTip(QString::fromUtf8(channel.resource.data()) +
                                                QStringLiteral(" · %1/%2/%3")
                                                    .arg(channel.msb)
                                                    .arg(channel.lsb)
                                                    .arg(channel.program));
            }
        }
        const bool loading = bindings_.loading();
        if (wasLoading_ && !loading)
        {
            information_ = bindings_.resources();
            refreshLists();
            message_->setText(QString());
        }
        wasLoading_ = loading;
        diagnostics_->setText(text("diagnostics", "Dropped / overflow / evicted / unsupported") +
                              QStringLiteral(": %1 / %2 / %3 / %4")
                                  .arg(status_.dropped)
                                  .arg(status_.overflow)
                                  .arg(status_.evicted)
                                  .arg(status_.unsupported));
    }

  public:
    MidiSynthWindow(Settings settings, MidiSynthBindings bindings)
        : bindings_(std::move(bindings)), settings_(std::move(settings))
    {
        auto *outer = new QHBoxLayout(this);
        auto *left = new QWidget(this);
        left->setMinimumWidth(fontMetrics().horizontalAdvance("Preferred resource and instrument") +
                              24);
        auto *resources = new QVBoxLayout(left);
        auto *catalogCard = new Controls::GroupPanel({}, left);
        auto *catalogLayout = new QVBoxLayout(catalogCard);
        resources->addWidget(catalogCard, 1);
        auto *actions = new QHBoxLayout;
        import_ = new Controls::ActionButton({}, left);
        import_->setObjectName("synth-import");
        remove_ = new Controls::ActionButton({}, left);
        remove_->setObjectName("synth-remove");
        actions->addWidget(import_);
        actions->addWidget(remove_);
        catalogLayout->addLayout(actions);
        tabs_ = new Controls::TabField(left);
        tabs_->setObjectName("synth-formats");
        for (unsigned format = 0; format < 2; ++format)
        {
            lists_[format] = new Controls::ListField(tabs_);
            lists_[format]->setObjectName(format == 0 ? "synth-sf2-list" : "synth-sfz-list");
            tabs_->addTab(lists_[format], format == 0 ? "SF2" : "SFZ");
            connect(lists_[format], &QListWidget::currentItemChanged, this,
                    [this](QListWidgetItem *item) {
                        selectedId_ = item ? item->data(Qt::UserRole).toString().toStdString()
                                           : std::string();
                        refreshDetails();
                    });
        }
        connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
            const auto *item = lists_[index]->currentItem();
            selectedId_ = item ? item->data(Qt::UserRole).toString().toStdString() : std::string();
            refreshDetails();
        });
        catalogLayout->addWidget(tabs_, 1);
        auto *informationCard = new Controls::GroupPanel({}, left);
        auto *informationLayout = new QVBoxLayout(informationCard);
        resources->addWidget(informationCard);
        details_ = new Controls::TextLabel(left);
        details_->setWordWrap(true);
        details_->setTextFormat(Qt::PlainText);
        informationLayout->addWidget(details_);
        mappingLabel_ = new Controls::TextLabel(left);
        mappingLabel_->setWordWrap(true);
        informationLayout->addWidget(mappingLabel_);
        auto *mapping = new QHBoxLayout;
        for (unsigned index = 0; index < 3; ++index)
        {
            mapping_[index] = new Controls::IntegerField(left);
            mapping_[index]->setObjectName(QStringLiteral("synth-mapping-%1").arg(index));
            mapping_[index]->setRange(0, 127);
            mapping->addWidget(mapping_[index]);
            connect(mapping_[index], &QSpinBox::valueChanged, this, [this] { changeMapping(); });
        }
        informationLayout->addLayout(mapping);
        prefer_ = new Controls::ActionButton({}, left);
        prefer_->setObjectName("synth-prefer");
        informationLayout->addWidget(prefer_);
        message_ = new Controls::TextLabel(left);
        message_->setWordWrap(true);
        informationLayout->addWidget(message_);
        outer->addWidget(left, 1);
        auto *right = new QVBoxLayout;
        auto *channelCard = new Controls::GroupPanel({}, this);
        auto *channelLayout = new QVBoxLayout(channelCard);
        auto *channels = new MidiSynthChannelPanel(channelCard);
        channelLayout->addWidget(channels);
        auto *grid = new QGridLayout(channels);
        grid->setVerticalSpacing(2);
        for (unsigned column = 0; column < 5; ++column)
        {
            headings_[column] = new Controls::TextLabel(channels);
            grid->addWidget(headings_[column], 0, column == 4 ? 5 : int(column), 1,
                            column == 3 ? 2 : 1);
        }
        for (unsigned channel = 0; channel < 16; ++channel)
        {
            const int row = int(channel + 1);
            grid->addWidget(new Controls::TextLabel(QString::number(channel + 1), channels), row,
                            0);
            instruments_[channel] = new Controls::TextLabel(channels);
            instruments_[channel]->setTextFormat(Qt::PlainText);
            instruments_[channel]->setMinimumWidth(
                fontMetrics().horizontalAdvance("No matching instrument") + 16);
            grid->addWidget(instruments_[channel], row, 1);
            gains_[channel] = gainField(QStringLiteral("synth-gain-%1").arg(channel), channels);
            channels->rows[channel] = gains_[channel];
            grid->addWidget(gains_[channel], row, 2);
            mutes_[channel] = switchButton(QStringLiteral("synth-mute-%1").arg(channel), channels);
            solos_[channel] = switchButton(QStringLiteral("synth-solo-%1").arg(channel), channels);
            grid->addWidget(mutes_[channel], row, 3);
            grid->addWidget(solos_[channel], row, 4);
            addMeter(grid, channel, row, 5, channels);
            gains_[channel]->onChanged([this, channel](double gain) {
                auto candidate = settings_;
                candidate.channels[channel].gainDb = gain;
                commit(std::move(candidate));
            });
            connect(mutes_[channel], &QPushButton::toggled, this, [this, channel](bool mute) {
                auto candidate = settings_;
                candidate.channels[channel].mute = mute;
                commit(std::move(candidate));
            });
            connect(solos_[channel], &QPushButton::toggled, this, [this, channel](bool solo) {
                auto candidate = settings_;
                candidate.channels[channel].solo = solo;
                commit(std::move(candidate));
            });
        }
        grid->setColumnStretch(5, 1);
        channels->firstColumn = headings_[0];
        channels->lastColumn = levels_[0];
        right->addWidget(channelCard, 1);
        master_ = new Controls::GroupPanel({}, this);
        auto *masterLayout = new QGridLayout(master_);
        masterGain_ = gainField("synth-master-gain", master_, true);
        masterMute_ = switchButton("synth-master-mute", master_);
        masterLayout->addWidget(masterGain_, 0, 0, 2, 1);
        masterLayout->addWidget(masterMute_, 0, 1, 2, 1);
        masterLayout->addWidget(new Controls::TextLabel("L", master_), 0, 2);
        masterLayout->addWidget(new Controls::TextLabel("R", master_), 1, 2);
        addMeter(masterLayout, 16, 0, 3, master_);
        addMeter(masterLayout, 17, 1, 3, master_);
        masterLayout->setColumnStretch(3, 1);
        right->addWidget(master_);
        diagnostics_ = new Controls::TextLabel(this);
        right->addWidget(diagnostics_);
        outer->addLayout(right, 3);
        masterGain_->onChanged([this](double gain) {
            auto candidate = settings_;
            candidate.masterDb = gain;
            commit(std::move(candidate));
        });
        connect(masterMute_, &QPushButton::toggled, this, [this](bool mute) {
            auto candidate = settings_;
            candidate.masterMute = mute;
            commit(std::move(candidate));
        });
        connect(import_, &QPushButton::clicked, this, [this] {
            const auto format = tabs_->currentIndex() == 0 ? "sf2" : "sfz";
            const auto path = QFileDialog::getOpenFileName(
                this, text("import", "Import"), {},
                QStringLiteral("%1 (*.%2)").arg(QString::fromLatin1(format).toUpper(), format));
            if (!path.isEmpty())
            {
                importFile(path);
            }
        });
        connect(remove_, &QPushButton::clicked, this, [this] {
            auto candidate = settings_;
            std::erase_if(candidate.resources,
                          [this](const auto &resource) { return resource.id == selectedId_; });
            if (candidate.preferred == selectedId_)
            {
                candidate.preferred.clear();
            }
            commit(std::move(candidate));
        });
        connect(prefer_, &QPushButton::clicked, this, [this] {
            auto candidate = settings_;
            candidate.preferred = selectedId_;
            commit(std::move(candidate));
        });
        bindRefreshTimer(refresh_, this);
        refresh_.setObjectName("synth-refresh");
        connect(&refresh_, &QTimer::timeout, this, [this] { refreshAudio(); });
        information_ = bindings_.resources();
        setSettings(settings_);
        language();
        bindMidiToolsTheme(this, "module-midi-tools-sf2-loader");
        resize(sizeHint());
    }
    void setSettings(const Settings &settings)
    {
        settings_ = settings;
        for (unsigned channel = 0; channel < 16; ++channel)
        {
            QSignalBlocker gain(gains_[channel]), mute(mutes_[channel]), solo(solos_[channel]);
            gains_[channel]->setValue(settings.channels[channel].gainDb);
            mutes_[channel]->setChecked(settings.channels[channel].mute);
            solos_[channel]->setChecked(settings.channels[channel].solo);
        }
        QSignalBlocker gain(masterGain_), mute(masterMute_);
        masterGain_->setValue(settings.masterDb);
        masterMute_->setChecked(settings.masterMute);
        wasLoading_ = true;
        refreshLists();
    }
    bool importFile(const QString &path)
    {
        const QFileInfo file(path);
        for (const auto &existing : settings_.resources)
        {
            const auto existingPath =
                QFileInfo(QString::fromStdString(existing.path)).absoluteFilePath();
            if (existingPath.compare(file.absoluteFilePath(), Qt::CaseInsensitive) == 0)
            {
                message_->setText(text("rejected", "Invalid or duplicate resource/settings."));
                return false;
            }
        }
        auto candidate = settings_;
        Resource resource;
        resource.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        resource.path = file.absoluteFilePath().toStdString();
        resource.format = file.suffix().toLower().toStdString();
        if (resource.format == "sfz")
        {
            for (unsigned program = 0; program < 128; ++program)
            {
                const bool occupied =
                    std::any_of(candidate.resources.begin(), candidate.resources.end(),
                                [program](const auto &other) {
                                    return other.format == "sfz" && other.msb == 0 &&
                                           other.lsb == 0 && other.program == program;
                                });
                if (!occupied)
                {
                    resource.program = uint8_t(program);
                    break;
                }
            }
        }
        candidate.resources.push_back(resource);
        if (!commit(std::move(candidate), true))
        {
            return false;
        }
        tabs_->setCurrentIndex(resource.format == "sf2" ? 0 : 1);
        selectedId_ = resource.id;
        refreshLists();
        return true;
    }
    void language()
    {
        setWindowTitle(text("name", "SF2/SFZ loader"));
        import_->setText(text("import", "Import"));
        remove_->setText(text("remove", "Remove reference"));
        prefer_->setText(text("usePreferred", "Use as preferred resource"));
        prefer_->setToolTip(
            text("browseHelp", "Select a file to inspect it. Browsing does not change playback."));
        mappingLabel_->setText(text("mapping", "SFZ mapping: Bank MSB / LSB / Program (0–127)"));
        for (unsigned index = 0; index < 3; ++index)
        {
            mapping_[index]->setAccessibleName(text(index == 0   ? "msb"
                                                    : index == 1 ? "lsb"
                                                                 : "program",
                                                    index == 0   ? "Bank MSB"
                                                    : index == 1 ? "Bank LSB"
                                                                 : "Program (0–127)"));
            mapping_[index]->setToolTip(mapping_[index]->accessibleName());
        }
        const char *keys[]{"channel", "instrument", "gain", "mute", "level"};
        const char *fallbacks[]{"Ch", "Current instrument", "Gain", "Mute / Solo", "Level"};
        for (unsigned index = 0; index < 5; ++index)
        {
            headings_[index]->setText(text(keys[index], fallbacks[index]));
            headings_[index]->setMinimumWidth(
                headings_[index]->fontMetrics().horizontalAdvance(headings_[index]->text()) + 6);
        }
        headings_[3]->setText(text("mute", "Mute") + " / " + text("solo", "Solo"));
        for (unsigned channel = 0; channel < 16; ++channel)
        {
            mutes_[channel]->setText("M");
            solos_[channel]->setText("S");
            mutes_[channel]->setAccessibleName(text("mute", "Mute") + " " +
                                               QString::number(channel + 1));
            solos_[channel]->setAccessibleName(text("solo", "Solo") + " " +
                                               QString::number(channel + 1));
            mutes_[channel]->setToolTip(mutes_[channel]->accessibleName());
            solos_[channel]->setToolTip(solos_[channel]->accessibleName());
            gains_[channel]->setAccessibleName(text("gain", "Gain") + " " +
                                               QString::number(channel + 1));
        }
        master_->setTitle(text("master", "Master"));
        masterMute_->setText("M");
        masterMute_->setAccessibleName(text("master", "Master") + " " + text("mute", "Mute"));
        masterMute_->setToolTip(masterMute_->accessibleName());
        masterGain_->setAccessibleName(text("master", "Master") + " " + text("gain", "Gain"));
        refreshLists();
        refreshAudio();
    }

  protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        clock_.start();
        refresh_.start();
    }
    void hideEvent(QHideEvent *event) override
    {
        refresh_.stop();
        QWidget::hideEvent(event);
    }
};
} // namespace WinJACKNexus::UI
