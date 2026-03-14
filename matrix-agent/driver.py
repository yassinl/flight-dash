#!/usr/bin/env python3
"""
Flight dashboard driver.
Renders flight info to the RGB LED matrix using the hzeller rpi-rgb-led-matrix Python bindings.
"""

import time
import sys
import os

# ---------------------------------------------------------------------------
# Dummy flight data — replace with OpenSky API integration (see scratch.md)
# ---------------------------------------------------------------------------
FLIGHT_DATA = {
    "airline":     "Delta Flight",
    "latitude":    "Latitude 10",
    "longitude":   "Longitude 40",
    "origin":      "Seattle Tacoma",
    "destination": "Paris Charles",
}

# Path to a BDF font shipped with the hzeller library.
# Adjust if fonts are installed elsewhere on the Pi.
FONT_PATH = os.path.join(os.path.dirname(__file__), "fonts", "5x8.bdf")

SCROLL_DELAY = 0.04   # seconds between each 1-pixel scroll step
LOOP_PAUSE   = 2.0    # seconds to pause before restarting scroll

try:
    from rgbmatrix import RGBMatrix, RGBMatrixOptions, graphics
except ImportError:
    print("ERROR: rgbmatrix module not found.")
    print("Build and install the hzeller rpi-rgb-led-matrix Python bindings on the Pi.")
    sys.exit(1)


def build_matrix() -> RGBMatrix:
    options = RGBMatrixOptions()
    options.rows             = 32       # physical rows on your panel
    options.cols             = 64       # physical cols on your panel
    options.chain_length     = 1
    options.parallel         = 1
    options.hardware_mapping = "regular"
    options.gpio_slowdown    = 2        # increase if you see flickering
    return RGBMatrix(options=options)


def main() -> None:
    matrix = build_matrix()
    canvas = matrix.CreateFrameCanvas()

    font = graphics.Font()
    font.LoadFont(FONT_PATH)

    yellow = graphics.Color(255, 215,   0)
    white  = graphics.Color(255, 255, 255)
    cyan   = graphics.Color(  0, 200, 255)

    lines = [
        (FLIGHT_DATA["airline"],     yellow),
        (FLIGHT_DATA["origin"],      white),
        (FLIGHT_DATA["destination"], white),
        (FLIGHT_DATA["latitude"],    cyan),
        (FLIGHT_DATA["longitude"],   cyan),
    ]

    line_height    = font.height + 2
    total_height   = len(lines) * line_height
    display_height = matrix.height

    pos = display_height  # start below the visible area

    try:
        while True:
            canvas.Clear()
            for i, (text, color) in enumerate(lines):
                y = pos + i * line_height
                if -line_height < y <= display_height + line_height:
                    graphics.DrawText(canvas, font, 2, y, color, text)

            canvas = matrix.SwapOnVSync(canvas)
            pos -= 1

            if pos < -(total_height):
                pos = display_height
                time.sleep(LOOP_PAUSE)

            time.sleep(SCROLL_DELAY)

    except KeyboardInterrupt:
        matrix.Clear()


if __name__ == "__main__":
    main()
