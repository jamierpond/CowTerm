#include "InterfaceOrientation.h"

#import <UIKit/UIKit.h>

namespace cowterm::mobile
{
namespace
{
void requestInterfaceOrientations(eacp::Graphics::Window& window, UIInterfaceOrientationMask orientations)
{
    auto* uiWindow = (__bridge UIWindow*) window.getHandle();
    auto* scene = uiWindow.windowScene;

    if (scene == nil)
        return;

    if (@available(iOS 16.0, *))
    {
        auto* preferences = [[UIWindowSceneGeometryPreferencesIOS alloc]
            initWithInterfaceOrientations:orientations];
        [scene requestGeometryUpdateWithPreferences:preferences errorHandler:nil];
        [preferences release];
    }
}
} // namespace

void requestLandscapeInterfaceOrientation(eacp::Graphics::Window& window)
{
    requestInterfaceOrientations(window, UIInterfaceOrientationMaskLandscapeRight);
}

void requestPortraitInterfaceOrientation(eacp::Graphics::Window& window)
{
    requestInterfaceOrientations(window, UIInterfaceOrientationMaskPortrait);
}
} // namespace cowterm::mobile
