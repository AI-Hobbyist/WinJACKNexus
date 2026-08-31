#pragma once

#include <WinJACKNexus/UI/PaintableWidget.hpp>

namespace WinJACKNexus::UI {

class WINJACK_UI_API MeterWidget : public PaintableWidget {
    Q_OBJECT

public:
    using PaintableWidget::PaintableWidget;
    ~MeterWidget() override = default;
};

}