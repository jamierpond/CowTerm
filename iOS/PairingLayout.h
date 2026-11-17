#pragma once

#include <eacp/Graphics/Primitives/Primitives.h>

#include <optional>

// Where the pairing screen's QR code and its column of text and controls go,
// as plain maths the Mac's tests can check.
namespace cowterm::mobile
{
struct PairingAreas
{
    eacp::Graphics::Rect title;

    // Only while the key is shown, and not while the keyboard is up, so the
    // field stays in view.
    std::optional<eacp::Graphics::Rect> qrCode;
    eacp::Graphics::Rect column;
};

// The title across the top. Portrait: the QR code under it, centred, and the
// column under that. Landscape: the QR code on the left, the column on its
// right. With the keyboard up, or showsQrCode false: the column alone, ending
// on the keyboard.
PairingAreas calculatePairingAreas(eacp::Graphics::Rect screen,
                                   eacp::Graphics::Insets safeArea,
                                   float keyboardCoveredHeight,
                                   bool showsQrCode);
} // namespace cowterm::mobile
