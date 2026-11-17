// Prints the text of each QR code Core Image finds in a PNG, one per line;
// used by scripts/check-ios-pairing.sh to check the pairing screen's code.
//
//   swift scripts/read-qr-code.swift screenshot.png
import CoreImage
import Foundation

let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let image = CIImage(contentsOf: url),
      let detector = CIDetector(ofType: CIDetectorTypeQRCode, context: nil,
                                options: [CIDetectorAccuracy: CIDetectorAccuracyHigh])
else {
    FileHandle.standardError.write("cannot read \(url.path)\n".data(using: .utf8)!)
    exit(1)
}

for case let feature as CIQRCodeFeature in detector.features(in: image) {
    print(feature.messageString ?? "")
}
