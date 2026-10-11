// Shared composition of the public client-name control. No private visual style.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "JackBackend.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/MidiToolsTheme.hpp>
namespace WinJACKNexus::MidiTools
{
class ClientSettings
{
  protected:
    JackBackend &backend_;
    const std::vector<Core::PortIdentity> ports_;
    QString name_;
    QString detail_;
    QByteArray json_, actual_;
    std::vector<QPointer<QWidget>> widgets_;

  public:
    ClientSettings(JackBackend &backend, std::vector<Core::PortIdentity> ports, QString name)
        : backend_(backend), ports_(std::move(ports)), name_(std::move(name))
    {
    }
    virtual ~ClientSettings()
    {
        for (auto &widget : widgets_)
        {
            if (widget)
            {
                delete widget;
            }
        }
    }
    bool applyName(const QString &value)
    {
        const auto name = value.trimmed();
        if (!backend_.validName(name.toUtf8().toStdString()))
        {
            return false;
        }
        if (!backend_.active())
        {
            name_ = name;
            detail_.clear();
            return true;
        }
        const auto result = Core::renameClient(backend_, ports_, name.toUtf8().toStdString());
        if (!result.actualName.empty())
        {
            name_ = QString::fromUtf8(result.actualName.c_str());
        }
        detail_.clear();
        for (const auto &failed : result.failed)
        {
            detail_ += QString::fromUtf8((failed.source + " -> " + failed.destination).c_str()) +
                       QLatin1Char('\n');
        }
        return result.status == Core::RenameResult::applied ||
               result.status == Core::RenameResult::unchanged;
    }
    const char *actualName()
    {
        actual_ =
            (backend_.active() ? QString::fromStdString(backend_.actualName()) : name_).toUtf8();
        return actual_.constData();
    }
    const char *settingsJson()
    {
        json_ = QJsonDocument(
                    QJsonObject{{"version", 1}, {"clientName", QString::fromUtf8(actualName())}})
                    .toJson(QJsonDocument::Compact);
        return json_.constData();
    }
    bool restore(const char *json)
    {
        if (!json || strnlen_s(json, 65537) > 65536)
        {
            return false;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(json, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
        {
            return false;
        }
        const auto o = doc.object();
        if (o.size() != 2 || o["version"] != QJsonValue(1) || !o["clientName"].isString())
        {
            return false;
        }
        return applyName(o["clientName"].toString());
    }
    QWidget *createSettings(QWidget *parent)
    {
        auto *root = new QWidget(parent);
        widgets_.push_back(root);
        auto *layout = new QVBoxLayout(root);
        auto *detail = new UI::Controls::TextLabel(root);
        detail->setWordWrap(true);
        auto *control = new UI::Controls::ClientNameWidget(
            QString::fromUtf8(actualName()),
            [this, root, detail](const QString &requested) {
                const auto previous = QString::fromUtf8(actualName());
                const bool ok = applyName(requested);
                detail->setText(detail_);
                const auto actual = QString::fromUtf8(actualName());
                if (actual != requested && (actual != previous || ok))
                {
                    QTimer::singleShot(0, root, [root, actual] {
                        for (auto *child : root->children())
                        {
                            if (auto *c = dynamic_cast<UI::Controls::ClientNameWidget *>(child))
                            {
                                c->setClientName(actual);
                            }
                        }
                    });
                }
                return ok;
            },
            root);
        layout->addWidget(control);
        layout->addWidget(detail);
        const auto scope = ports_.size() == 3    ? "module-midi-tools-sf2-loader"
                           : ports_.size() == 17 ? "module-midi-tools-midi-spilit"
                                                 : "module-midi-tools-midi-matrix";
        UI::bindMidiToolsTheme(root, scope);
        return root;
    }
    void destroySettings(QWidget *widget)
    {
        for (auto &w : widgets_)
        {
            if (w == widget)
            {
                delete widget;
                w = nullptr;
                break;
            }
        }
    }
};
} // namespace WinJACKNexus::MidiTools
