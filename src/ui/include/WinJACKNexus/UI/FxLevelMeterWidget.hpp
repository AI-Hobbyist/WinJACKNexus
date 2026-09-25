#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <QWidget>

namespace WinJACKNexus::UI {

class WINJACK_UI_API FxLevelMeterWidget final : public QWidget {
public:
    explicit FxLevelMeterWidget(QWidget* parent = nullptr);
    void setLevels(double inputDb, double outputDb);
    double inputDb() const noexcept { return inputDb_; }
    double outputDb() const noexcept { return outputDb_; }
    QRectF barRect(int row) const noexcept;
    qreal positionForDb(double db) const noexcept;
    QSize sizeHint() const override { return {260, 86}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double inputDb_ = -60.0;
    double outputDb_ = -60.0;
};

}
