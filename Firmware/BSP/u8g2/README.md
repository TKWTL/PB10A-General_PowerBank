# u8g2 subset for PB10A SSD1306
This folder ports the trimmed u8g2 drawing core from IH01A.
The SSD1306 128x64 geometry driver uses u8g2's BSD-2-Clause license;
initialization commands are from upstream olikraus/u8g2
`csrc/u8x8_d_ssd1306_128x64_noname.c`.
The PB10A BSP owns the actual initialization and page-addressing
DMA transaction sequence. Do not call synchronous `u8g2_SendBuffer()`.
See `LICENSE` in this folder. Only the DPC27A Fusion Pixel p12 font
is built; no Chinese localization is enabled.
