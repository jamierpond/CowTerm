#!/usr/bin/env python3
"""Writes protanopia, deuteranopia and greyscale versions of a PNG screenshot
beside it (<name>-protanopia.png, <name>-deuteranopia.png, <name>-greyscale.png),
to check that what the phone shows by colour still reads for a red-green
colour-blind eye. Standard library only. The matrices are Machado, Oliveira and
Fernandes 2009 at severity 1.0, the same ones Tests/ColourBlindHostColoursTests.cpp
uses; greyscale is WCAG relative luminance.

    scripts/simulate-colour-vision.py build-ios-sim/screenshots/overview.png
"""

import struct
import sys
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
COLOUR_TYPE_RGB = 2
COLOUR_TYPE_RGBA = 6
CHANNELS_BY_COLOUR_TYPE = {COLOUR_TYPE_RGB: 3, COLOUR_TYPE_RGBA: 4}
SUPPORTED_BIT_DEPTH = 8

PROTANOPIA = (
    (0.152286, 1.052583, -0.204868),
    (0.114503, 0.786281, 0.099216),
    (-0.003882, -0.048116, 1.051998),
)
DEUTERANOPIA = (
    (0.367322, 0.860646, -0.227968),
    (0.280085, 0.672501, 0.047413),
    (-0.011820, 0.042940, 0.968881),
)
LUMINANCE_WEIGHTS = (0.2126, 0.7152, 0.0722)
GREYSCALE = (LUMINANCE_WEIGHTS, LUMINANCE_WEIGHTS, LUMINANCE_WEIGHTS)

# The sRGB transfer curve (IEC 61966-2-1).
CHANNEL_MAXIMUM = 255
SRGB_LINEAR_SEGMENT_END = 0.04045
SRGB_LINEAR_SEGMENT_END_IN_LINEAR_LIGHT = 0.0031308
SRGB_LINEAR_SEGMENT_SLOPE = 12.92
SRGB_CURVE_OFFSET = 0.055
SRGB_CURVE_EXPONENT = 2.4

SIMULATIONS = {"protanopia": PROTANOPIA, "deuteranopia": DEUTERANOPIA, "greyscale": GREYSCALE}


def decode_srgb_channel(level):
    encoded = level / CHANNEL_MAXIMUM
    if encoded <= SRGB_LINEAR_SEGMENT_END:
        return encoded / SRGB_LINEAR_SEGMENT_SLOPE
    return ((encoded + SRGB_CURVE_OFFSET) / (1 + SRGB_CURVE_OFFSET)) ** SRGB_CURVE_EXPONENT


def encode_srgb_channel(linear):
    clamped = min(1.0, max(0.0, linear))
    if clamped <= SRGB_LINEAR_SEGMENT_END_IN_LINEAR_LIGHT:
        encoded = clamped * SRGB_LINEAR_SEGMENT_SLOPE
    else:
        encoded = (1 + SRGB_CURVE_OFFSET) * clamped ** (1 / SRGB_CURVE_EXPONENT) - SRGB_CURVE_OFFSET
    return round(encoded * CHANNEL_MAXIMUM)


LINEAR_BY_LEVEL = [decode_srgb_channel(level) for level in range(CHANNEL_MAXIMUM + 1)]


def read_png_chunks(data):
    position = len(PNG_SIGNATURE)
    while position < len(data):
        (length,) = struct.unpack(">I", data[position : position + 4])
        kind = data[position + 4 : position + 8]
        yield kind, data[position + 8 : position + 8 + length]
        position += 12 + length


def paeth(left, up, up_left):
    estimate = left + up - up_left
    distances = (abs(estimate - left), abs(estimate - up), abs(estimate - up_left))
    if distances[0] <= distances[1] and distances[0] <= distances[2]:
        return left
    if distances[1] <= distances[2]:
        return up
    return up_left


def unfilter_row(filter_type, row, previous, pixel_size):
    out = bytearray(row)
    for index in range(len(out)):
        left = out[index - pixel_size] if index >= pixel_size else 0
        up = previous[index]
        up_left = previous[index - pixel_size] if index >= pixel_size else 0
        predictors = (0, left, up, (left + up) // 2, paeth(left, up, up_left))
        out[index] = (out[index] + predictors[filter_type]) & 0xFF
    return out


def is_supported_header(bit_depth, colour_type, interlace):
    return bit_depth == SUPPORTED_BIT_DEPTH and colour_type in CHANNELS_BY_COLOUR_TYPE and not interlace


def read_header_and_pixel_data(path):
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        sys.exit(f"{path} is not a PNG")
    chunks = list(read_png_chunks(data))
    header = next(body for kind, body in chunks if kind == b"IHDR")
    compressed = b"".join(body for kind, body in chunks if kind == b"IDAT")
    return struct.unpack(">IIBBBBB", header), zlib.decompress(compressed)


def unfilter_rows(raw, height, stride, pixel_size):
    rows = []
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        previous = unfilter_row(raw[start], raw[start + 1 : start + 1 + stride], previous, pixel_size)
        rows.append(previous)
    return rows


def read_png(path):
    (width, height, bit_depth, colour_type, _, _, interlace), raw = read_header_and_pixel_data(path)
    if not is_supported_header(bit_depth, colour_type, interlace):
        sys.exit(f"{path}: only 8-bit RGB or RGBA PNGs without interlacing are supported")
    pixel_size = CHANNELS_BY_COLOUR_TYPE[colour_type]
    return width, height, pixel_size, unfilter_rows(raw, height, width * pixel_size, pixel_size)


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    header = struct.pack(">IIBBBBB", width, height, SUPPORTED_BIT_DEPTH, COLOUR_TYPE_RGB, 0, 0, 0)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    path.write_bytes(PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def simulate_pixel(matrix, red, green, blue):
    linear = (LINEAR_BY_LEVEL[red], LINEAR_BY_LEVEL[green], LINEAR_BY_LEVEL[blue])
    return tuple(encode_srgb_channel(sum(weight * channel for weight, channel in zip(row, linear))) for row in matrix)


def simulate_image(matrix, width, pixel_size, rows):
    seen = {}
    out_rows = []
    for row in rows:
        out = bytearray(width * 3)
        for x in range(width):
            pixel = tuple(row[x * pixel_size : x * pixel_size + 3])
            if pixel not in seen:
                seen[pixel] = simulate_pixel(matrix, *pixel)
            out[x * 3 : x * 3 + 3] = bytes(seen[pixel])
        out_rows.append(out)
    return out_rows


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    width, height, pixel_size, rows = read_png(source)
    for name, matrix in SIMULATIONS.items():
        target = source.with_name(f"{source.stem}-{name}.png")
        write_png(target, width, height, simulate_image(matrix, width, pixel_size, rows))
        print(target)


if __name__ == "__main__":
    main()
