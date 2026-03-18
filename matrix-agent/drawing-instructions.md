# Drawing Images + Text on the Same LED Panel

## The Model

There is **one flat canvas** — the entire matrix (e.g. 64×32 pixels). There are no sub-panels or
regions in the library. Everything boils down to `SetPixel(x, y, r, g, b)`. Both `RGBMatrix` and
`FrameCanvas` implement the `Canvas` interface identically.

## Image + Text on the Same Canvas

Both operations accept x/y offsets, so you divide your canvas space manually:

**Image** — `CopyImageToCanvas` uses `offset_x`/`offset_y` to position the image anywhere on the
canvas. Scale the image to only the region you want (e.g. 32×32 for the left half of a 64×32
panel), then copy it with the appropriate offset.

**Text** — `DrawText(canvas, font, x, y, color, bg_color, text)` draws starting at any `(x, y)`.
Start it past the image region (e.g. `x = 34`).

`SetImage()` in `graphics.h` also accepts a raw RGB buffer with `canvas_offset_x`/`canvas_offset_y`
— useful for compositing without ImageMagick.

## No Separate Allocation Needed

You don't allocate a "panel" per region. Steps:
1. Scale the airline logo to (e.g.) 32×32 pixels
2. Copy it to the canvas at offset `(0, 0)`
3. Call `DrawText` starting at `x = 34` for flight info

Use a `FrameCanvas` for double-buffering: draw the full composed frame offscreen, then call
`SwapOnVSync()` to display atomically — no tearing between the image and text regions.

## Practical Layout (64×32 matrix)

```
[  airline logo  ][   flight text   ]
  0,0 → 31,31       x=34, various y
  (32×32 image)     (DrawText calls)
```

- Left 32×32 → airline logo (scaled via ImageMagick or raw buffer, copied at offset `0,0`)
- Right side → flight info text (`DrawText` starting at `x=34`)

Both drawn to the same `FrameCanvas`, swapped atomically with `SwapOnVSync()`.
