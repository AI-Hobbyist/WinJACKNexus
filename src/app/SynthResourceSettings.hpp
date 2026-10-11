// Optional global settings for the installed single-node SF2/SFZ loader.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QDirIterator>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <WinJACKNexus/Common/SynthResourceBudget.hpp>
#include <WinJACKNexus/Node/ModuleCompatibility.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/I18nManager.hpp>

namespace WinJACKNexus::App
{
class SynthResourceSettings final : public QWidget
{
  public:
    explicit SynthResourceSettings(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("sf2-loader-global-settings"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        card_ = new UI::Controls::GroupPanel({}, this);
        layout->addWidget(card_);
        auto *form = new QFormLayout(card_);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        label_ = new QLabel(card_);
        budget_ = new UI::Controls::IntegerField(card_);
        budget_->setObjectName(QStringLiteral("application.sf2LoaderResourceBudgetMiB"));
        budget_->setFixedHeight(UI::Controls::sidebarControlHeight);
        budget_->setRange(1, Common::MidiTools::maximumResourceBudgetMiB);
        budget_->setSuffix(QStringLiteral(" MiB"));
        budget_->setKeyboardTracking(false);
        budget_->setValue(Common::MidiTools::resourceBudgetMiB());
        form->addRow(label_, budget_);
        connect(budget_, qOverload<int>(&QSpinBox::valueChanged), this,
                [](int value) { Common::MidiTools::setResourceBudgetMiB(value); });
        if (auto *manager = UI::I18nManager::installed())
        {
            manager->bind(this, [this] { language(); });
        }
        else
        {
            language();
        }
        hide();
    }

    // Called when opening global settings, after module discovery/startup.
    // Validate exports without creating a node or opening a JACK client.
    void refreshAvailability(const QString &nodeRoot)
    {
        QString available;
        QDirIterator files(nodeRoot, {QStringLiteral("mod_sf2_loader.dll")}, QDir::Files,
                           QDirIterator::Subdirectories);
        while (files.hasNext())
        {
            const auto path = files.next();
            const auto library = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
            if (!library)
            {
                continue;
            }
            using Name = const char *(*)();
            const auto name = reinterpret_cast<Name>(GetProcAddress(library, "wjn_get_node_name"));
            const auto nodeName = Node::isSingleNodeModule(library) && name ? name() : nullptr;
            if (nodeName && std::strcmp(nodeName, "sf2_loader") == 0 &&
                GetProcAddress(library, "wjn_create_node") &&
                GetProcAddress(library, "wjn_destroy_node"))
            {
                available = path;
            }
            FreeLibrary(library);
            if (!available.isEmpty())
            {
                break;
            }
        }
        if (available != source_)
        {
            lease_ = {};
            source_ = available;
            if (auto *manager = UI::I18nManager::installed(); manager && !source_.isEmpty())
            {
                lease_ = manager->registerLibrary(source_, UI::I18nManager::Extension::node);
            }
        }
        const QSignalBlocker blocker(budget_);
        budget_->setValue(Common::MidiTools::resourceBudgetMiB());
        language();
        setVisible(!available.isEmpty());
    }

  private:
    void language()
    {
        auto *manager = UI::I18nManager::installed();
        const auto text = [manager](const char *key, const char *fallback)
        {
            return manager ? manager->text(QStringLiteral("midi.sf2_loader"),
                                           QString::fromLatin1(key), QString::fromUtf8(fallback))
                           : QString::fromUtf8(fallback);
        };
        card_->setTitle(text("name", "SF2/SFZ Loader"));
        label_->setText(text("globalBudget", "Resource budget (SF2 equivalent)"));
        budget_->setToolTip(
            text("globalBudgetHelp",
                 "Default: 64 MiB. SF2 files reserve eight times their file size; this value sets "
                 "the shared sample budget to eight times the selected amount, also used by SFZ. "
                 "Applies on the next resource load. Existing sounds keep playing."));
        budget_->setAccessibleName(label_->text());
        budget_->setAccessibleDescription(budget_->toolTip());
    }
    QLabel *label_ = nullptr;
    UI::Controls::GroupPanel *card_ = nullptr;
    UI::Controls::IntegerField *budget_ = nullptr;
    QString source_;
    UI::I18nDomainLease lease_;
};
} // namespace WinJACKNexus::App
