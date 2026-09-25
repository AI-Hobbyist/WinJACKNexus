#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <QWidget>
#include <QStringList>
#include <functional>
#include <memory>

namespace WinJACKNexus::UI {

class WINJACK_UI_API FxSettingsWindow final : public QWidget {
public:
    explicit FxSettingsWindow(const QString& sourceName, const QStringList& sourceChannels,
                              QWidget* parent = nullptr);
    ~FxSettingsWindow() override;
    void setChainEnabled(bool enabled);
    void setSourceChannels(const QStringList& channels);
    void setChainEnabledHandler(std::function<void(bool)> handler);
    void cloneChainFrom(const FxSettingsWindow& source);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
