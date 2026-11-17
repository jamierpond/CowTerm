#include "InterfaceOrientation.h"

#import <UIKit/UIKit.h>

namespace cowterm::mobile
{
void requestLandscapeInterfaceOrientation(eacp::Graphics::Window& window)
{
    auto* uiWindow = (__bridge UIWindow*) window.getHandle();
    auto* scene = uiWindow.windowScene;

    if (scene == nil)
        return;

    if (@available(iOS 16.0, *))
    {
        auto* preferences = [[UIWindowSceneGeometryPreferencesIOS alloc]
            initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeRight];
        [scene requestGeometryUpdateWithPreferences:preferences errorHandler:nil];
        [preferences release];
    }
}
} // namespace cowterm::mobile
