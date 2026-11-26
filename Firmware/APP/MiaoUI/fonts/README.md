# PB10A MiaoUI font subsets

Radio shares the 12px Fusion Pixel font; there is no separate 12px
ASCII font. The shared font uses the original fixed 6px ASCII advances.

## Spleen 24px status subset

The 24px Spleen font (Frederic Cambus; obtained from Collar-Radio) is now
`593` bytes rather than the original 10,579 bytes. It contains only:

```text
 .0123456789CISTacegiknru
```

The requested `.0123456789STegiknu` character set was expanded by the
characters in `I2CScanner` and a space, so both `Seeking...` and `Tuning...`
and the I2C-scanner splash `I2C` / `Scanner` continue to render.
Every kept glyph retains its original u8g2 RLE payload and metrics; the
ASCII offsets and empty Unicode index are rebuilt.

The radio main screen, its tuning/seeking status and the I2C scanner
remain ASCII; no 24px Chinese glyphs are needed.

## Shared 12px Chinese subset

`font_fusion_pixel_p12_zhcn` remains the only Chinese-capable font.
The 262 Han glyphs are from the supplied
`fusion-pixel-12px-proportional-zh_hans.bdf` (FONT_VERSION 2026.09.25).
The 96 Latin/degree glyphs are restored from PB10A's earlier
MiaoUI-based font, byte-for-byte. The Han glyph records and Unicode
index are unchanged. The 358-glyph array occupies 8,130 bytes
(previously 7,954); the ASCII/degree advance is 6px and Han advance
is 12px. Layout still measures actual text width.

`font_home_h6w4` remains byte-for-byte identical. SOS and magnetic
calibration no longer use it; powerbank, I2C scanner, game interiors
and portrait toast retain their original English display.

This encoding is **not** the generic four-mode byte RLE in `Firmware/LIB/`.
Original font attribution and source license obligations remain applicable.
