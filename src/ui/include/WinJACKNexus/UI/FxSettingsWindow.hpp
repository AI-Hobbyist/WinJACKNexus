#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
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
    void addDefaultMasterEffects();
    void setSourceName(const QString& name);
    void loadChain(const std::vector<Common::MixerEffectState>& effects, bool enabled);
    void setChainCommitHandler(std::function<bool(const std::vector<Common::MixerEffectState>&, bool)> handler);
    void setMeterReadHandler(std::function<float(bool)> handler);
    void setChannelMeterReadHandler(std::function<float(bool, std::size_t)> handler);
    void setLiveAnalysisReadHandler(std::function<bool(std::size_t, float[8], float[8],
        float[8], void*, std::uint32_t)> handler);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
