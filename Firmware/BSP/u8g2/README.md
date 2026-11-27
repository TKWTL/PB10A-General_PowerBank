# u8g2 subset for PB10A SSD1306
This folder ports the trimmed u8g2 drawing core from IH01A.
The SSD1306 128x64 geometry driver uses u8g2's BSD-2-Clause license;
initialization commands are from upstream olikraus/u8g2
`csrc/u8x8_d_ssd1306_128x64_noname.c`.
The PB10A BSP owns the actual initialization and page-addressing
DMA transaction sequence. Do not call synchronous `u8g2_SendBuffer()`.
See `LICENSE` in this folder and the original source copyright headers.
The PB10A application builds three font subsets in
[`APP/MiaoUI/fonts`](../../APP/MiaoUI/fonts/README.md): shared 12px
Chinese/Latin (8175 B), small h6w4 (723 B), and 24px Spleen (593 B).
MiaoUI owns English/Chinese text selection; u8g2 only draws glyphs.
The Unicode font RLE is unrelated to the generic byte RLE in `LIB/`.
