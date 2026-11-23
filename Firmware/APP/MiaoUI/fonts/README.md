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

Future localized `调谐中...` and `搜索中...` will need corresponding
**24px Unicode glyphs** encoded with this font's u8g2 parameters, and an
updated Unicode lookup table; merely adding arbitrary UTF-8 characters
to the ASCII whitelist will not display Chinese. Alternatively, switch
to an existing Chinese-capable font in the radio status screen.

This encoding is **not** the generic four-mode byte RLE in `Firmware/LIB/`.
Original font attribution and source license obligations remain applicable.
