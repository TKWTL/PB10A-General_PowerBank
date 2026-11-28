# PB10A OLED bring-up (historical record)

> 历史联调记录：以下正文保留早期 OLED 接入阶段的配置、预期行为与验证边界，不是当前固件的操作或实现说明。现行实现见 [Firmware 总览](../README.md) 和 [BSP](../BSP/README.md)。

Scope: make the STM32G070KBTx drive a 128x64 SSD1306 OLED over
SPI1 TX DMA with the MiaoUI/u8g2 drawing stack and cooperative coroOS.
No PD negotiation, charging, Chinese UI, power management or additional
peripheral drivers are started by the application.

## GPIO / SPI assignment (CubeMX UserLabel in Core/Inc/main.h)

| Signal | Pin | Use |
|---|---|---|
| SCK | PA1 | SPI1_SCK |
| MOSI | PA2 | SPI1_MOSI |
| D/C | PA3 (D_C) | 0=commands / 1=framebuffer data |
| CS | PA4 | SPI1_NSS hardware output |
| CPEN | PB2 | Enable OLED power/boost before initialization |
| OLED RESET | MCU NRST | Shared hardware reset; no separate GPIO |
| SPI DMA | DMA1 Channel 1 | SPI1 TX |
| UP | PA9 | menu previous |
| DOWN | PA10 | menu next |
| LEFT | PA15 | back |
| RIGHT | PC6 | enter |
| CONF | PA12 | enter |

All five keys use the CubeMX input pull-ups, pressed=low.
The adapter polls at the MiaoUI cadence (20ms), applies 25ms stable
debounce, and emits one action per press; currently no key autorepeat.
CubeMX EXTI handlers remain but are intentionally unused for UI input.

## OLED startup and transfers

1. `MX_GPIO_Init`, `MX_DMA_Init` and `MX_SPI1_Init` first.
2. `SSD1306_Display_Begin` drives `CPEN` high, waits **30ms** cooperatively.
3. The nonblocking SPI DMA initialization sends the SSD1306 128x64
   'noname' sequence from `olikraus/u8g2`, adjusted for **page
   addressing**: charge pump **0x8D 0x14** (enabled), **0x20 0x02**
   (page mode); **0xAF** enables display output.
4. `SSD1306_Display_Service` drives command/data phases: for every
   page 0..7, send **B0|page, 00, 10** with D/C=0 and 128 data
   bytes with D/C=1; HAL SPI1 DMA sources remain static until completion.
5. ISR callbacks only set the SPI transfer state. Foreground service
   observes completion, retries recoverable errors and sends the next
   page. No blocking `HAL_SPI_Transmit` or `HAL_Delay`.
6. Once initialized, the coroOS MiaoUI task draws an English-only
   demo menu at 20ms intervals. Font is the DPC27A Fusion Pixel p12
   resource retaining its ASCII glyphs, without DPC27A's 128x80 layout
   or top status bar. No separate Chinese-localization module is built.

The SPI bitrate is set to **8MHz** in the CubeMX `.ioc` and also
enforced in the SPI1 post-init user block; hardware NSS pulse mode
is disabled there so the panel does not see a CS pulse per byte.
Do not regenerate `Core` without reviewing the `USER CODE` regions.

## Expected first test

With the OLED attached and powered, flash and reset. After supply
settling and DMA initialization, the screen should show the PB10A
MiaoUI text menu: `OLED Test`, `Display`, `About`.
Selecting `OLED Test` should display the 4-line status information;
LEFT should return. Display contrast, inversion, screen flip and
burn-in checks are available from Display.

No physical test or firmware build was conducted here. If the
screen is blank, inspect PB2 CPEN, PA4 NSS, PA3 D/C, PA1 SCK, PA2 MOSI,
the OLED power rail and DMA1 Channel1 interrupt before changing UI.
The user will supply any ArmClang/Keil diagnostics.

## Full-frame zero-copy SPI DMA

The SSD1306 initialization sequence is unchanged (page addressing, `0x20 0x02`). On each frame the foreground service selects horizontal addressing (`0x20 0x00`), columns 0–127 and pages 0–7, then starts **one 1024-byte TX DMA** directly from `u8g2_GetBufferPtr()`. After DMA completion it restores page addressing (`0x20 0x02`). No page buffer or frame memcpy is used. The cooperative MiaoUI thread must not draw while `SSD1306_Display_FrameBusy()` is nonzero. Timeout/abort and transfer completion release the framebuffer only after safe completion or eventual recovery.

u8g2 sources are under `Firmware/BSP/u8g2/`, not CubeMX-managed `ThirdParty/`.
