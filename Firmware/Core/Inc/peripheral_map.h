#ifndef PB10A_PERIPHERAL_MAP_H
#define PB10A_PERIPHERAL_MAP_H

/* Keep the runtime bindings aligned with the CubeMX .ioc file. */
#define PB_SPI_INSTANCE SPI1
#define PB_SPI_INIT MX_SPI1_Init
#define PB_SPI_TX_DMA_INSTANCE DMA1
#define PB_SPI_TX_DMA_CHANNEL LL_DMA_CHANNEL_1
#define PB_SPI_TX_DMA_IRQ DMA1_Channel1_IRQn

#define PB_I2C_INSTANCE I2C1

#define PB_ADC_INSTANCE ADC1
#define PB_ADC_COMMON __LL_ADC_COMMON_INSTANCE(PB_ADC_INSTANCE)
#define PB_ADC_DMA_INSTANCE DMA1
#define PB_ADC_DMA_CHANNEL LL_DMA_CHANNEL_2
#define PB_ADC_DMA_IRQ DMA1_Channel2_3_IRQn

/* Zero-based offsets in the DMA buffer, in CubeMX regular rank order. */
#define PB_ADC_AUX_CURRENT_SLOT 0u
#define PB_ADC_AUX_VOLTAGE_SLOT 1u
#define PB_ADC_TEMPERATURE_SLOT 2u
#define PB_ADC_VREF_SLOT 3u
#define PB_ADC_SCAN_COUNT 4u

/* STM32G0 packs four ISR/IFCR bits per zero-based LL DMA channel. */
#define PB_DMA_CHANNEL_FLAGS(channel, flags_ch1) ((flags_ch1) << (4u * (channel)))

#endif
