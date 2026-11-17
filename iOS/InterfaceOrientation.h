#pragma once

#include <eacp/Graphics/Window/Window.h>

namespace cowterm::mobile
{
// Turns the window's interface to landscape, with no device rotation behind it.
// Lets a headless simulator, which simctl cannot rotate, show the landscape
// layout.
void requestLandscapeInterfaceOrientation(eacp::Graphics::Window& window);

// Turns it back to portrait. A simulator keeps the last interface orientation
// across launches and reboots, so a portrait screenshot has to ask for it.
void requestPortraitInterfaceOrientation(eacp::Graphics::Window& window);
} // namespace cowterm::mobile
