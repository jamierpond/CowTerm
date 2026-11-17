#!/bin/sh
# Writes the iPhone app's icons, in every size iOS asks an iPhone app for,
# from the Mac app's cow (Terminal/Icon.svg): the same artwork on the
# terminal's own background colour, because iOS wants opaque squares and
# rounds the corners itself. Icon.svg carries the cow as a 1024-pixel PNG, so
# each size is that bitmap scaled down, never up.
#
#   scripts/make-ios-app-icon.sh
set -eu
cd "$(dirname "$0")/.."
terminal_background="#1a1b26"
icon_set="iOS/Resources/Assets.xcassets/AppIcon.appiconset"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The PNG inside Icon.svg's data URI, so the SVG stays the one source.
sed -n 's/.*xlink:href="data:image\/png;base64,\([^"]*\)".*/\1/p' Terminal/Icon.svg | base64 -d > "$work/cow.png"

# point size, scale, idiom: the iPhone home screen, Spotlight, Settings and
# notifications at 2x and 3x, and the App Store's 1024.
sizes="20 2 iphone
20 3 iphone
29 2 iphone
29 3 iphone
40 2 iphone
40 3 iphone
60 2 iphone
60 3 iphone
1024 1 ios-marketing"

echo "$sizes" | while read -r points scale idiom; do
    pixels=$((points * scale))
    file="AppIcon-${points}@${scale}x.png"
    magick "$work/cow.png" -background "$terminal_background" -alpha remove -alpha off \
        -filter Lanczos -resize "${pixels}x${pixels}" -strip "$icon_set/$file"
    printf '    {"filename": "%s", "idiom": "%s", "scale": "%sx", "size": "%sx%s"},\n' \
        "$file" "$idiom" "$scale" "$points" "$points"
done > "$work/images"

{
    echo '{'
    echo '  "images": ['
    sed '$ s/,$//' "$work/images"
    echo '  ],'
    echo '  "info": {"author": "xcode", "version": 1}'
    echo '}'
} > "$icon_set/Contents.json"
ls "$icon_set"
