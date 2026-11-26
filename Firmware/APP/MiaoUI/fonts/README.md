# PB10A MiaoUI font subsets

The Radio 12px ASCII font was removed: all **95 printable ASCII glyphs** from
`font_radio_h12w6` and the ASCII part of `font_fusion_pixel_p12_zhcn`
have identical decoded pixels, offsets and 6px advances despite different
u8g2 RLE encoding parameters. Radio now uses the latter font directly.

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
The original 96 Latin/degree and 118 Han glyph payloads are unchanged.
185 missing Han glyphs are added from the official Fusion Pixel
12px Mono zh_hans BDF release **2026.11.10**, aligned to the existing
baseline. The 399-glyph array is 9,190 bytes (previously 4,342);
the Unicode lookup is indexed in blocks of 32 glyphs.
The complete catalog and settings language selector are covered.

`font_home_h6w4` remains byte-for-byte identical. SOS and magnetic
calibration no longer use it; powerbank, I2C scanner, game interiors
and portrait toast retain their original English display.

This encoding is **not** the generic four-mode byte RLE in `Firmware/LIB/`.
Original font attribution and source license obligations remain applicable.
