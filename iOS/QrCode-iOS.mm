#include "QrCode.h"

#import <CoreImage/CoreImage.h>


namespace cowterm::mobile
{
namespace
{
constexpr auto bytesPerPixel = 4;
constexpr auto darkThreshold = 128;
}

std::optional<QrCode> makeQrCode(const std::string& text)
{
    CIFilter* generator = [CIFilter filterWithName:@"CIQRCodeGenerator"];
    [generator setValue:[NSData dataWithBytes:text.data() length:text.size()] forKey:@"inputMessage"];
    [generator setValue:@"M" forKey:@"inputCorrectionLevel"];
    CIImage* image = generator.outputImage;

    if (image == nil)
        return std::nullopt;

    const auto size = static_cast<int>(image.extent.size.width);
    auto pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(size * size * bytesPerPixel));
    CIContext* context = [CIContext contextWithOptions:nil];
    CGColorSpaceRef colourSpace = CGColorSpaceCreateDeviceRGB();
    [context render:image
           toBitmap:pixels.data()
           rowBytes:size * bytesPerPixel
             bounds:image.extent
             format:kCIFormatRGBA8
         colorSpace:colourSpace];
    CGColorSpaceRelease(colourSpace);

    auto code = QrCode {size, {}};
    code.darkModules.reserve(static_cast<std::size_t>(size * size));

    for (auto index = std::size_t {0}; index < pixels.size(); index += bytesPerPixel)
        code.darkModules.push_back(pixels[index] < darkThreshold);

    return code;
}
} // namespace cowterm::mobile
