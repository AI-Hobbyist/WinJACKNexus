#include "ServiceTrayIcon.h"

#include "ServiceApplication.h"

#include <ServiceBinaryData.h>

namespace wjn::adapter::service
{

ServiceTrayIcon::ServiceTrayIcon (ServiceApplication& owner)
    : application (owner)
{
    const auto image = juce::ImageFileFormat::loadFrom (
        ServiceBinaryData::adapter_service_transparent_png,
        ServiceBinaryData::adapter_service_transparent_pngSize);
    setIconImage (image, image);
    setIconTooltip (application.getApplicationName());
}

void ServiceTrayIcon::mouseDown (const juce::MouseEvent& /*event*/)
{
    juce::PopupMenu menu;
    menu.addItem (1, application.localizedText ("adapterService.menu.status", "当前状态：")
                         + application.statusText(),
                  false, false);
    menu.addSeparator();
    menu.addItem (2, application.localizedText ("adapterService.action.exit", "退出"));
    auto* serviceApplication = &application;
    menu.showMenuAsync (juce::PopupMenu::Options(), [serviceApplication] (int result)
    {
        if (result == 2)
            serviceApplication->requestQuit();
    });
}

} // namespace wjn::adapter::service