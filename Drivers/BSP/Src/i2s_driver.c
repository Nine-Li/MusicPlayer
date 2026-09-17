#include "i2s_driver.h"
#include "main.h"
#include <math.h>

I2S_HandleTypeDef i2s2_handle = {0};
DMA_HandleTypeDef i2s_dmatx = {0};
DMA_HandleTypeDef i2s_dmarx = {0};
RCC_PLLI2SInitTypeDef plli2s = {0};

void i2s2_init(void)
{
    i2s2_handle.Instance = SPI2;
    i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_48K;
    i2s2_handle.Init.ClockSource = I2S_CLOCK_PLL;
    i2s2_handle.Init.CPOL = I2S_CPOL_LOW;
    i2s2_handle.Init.DataFormat = I2S_DATAFORMAT_16B;
    i2s2_handle.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    i2s2_handle.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    i2s2_handle.Init.Mode = I2S_MODE_MASTER_TX;
    i2s2_handle.Init.Standard = I2S_STANDARD_LSB;

    if (HAL_I2S_Init(&i2s2_handle) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2)
    {
        __HAL_RCC_SPI2_CLK_ENABLE();
        __HAL_RCC_DMA1_CLK_ENABLE();

        plli2s.PLLI2SN = 258;
        plli2s.PLLI2SR = 3;

        if (HAL_RCCEx_EnablePLLI2S(&plli2s) != HAL_OK)
        {
            Error_Handler();
        }

        GPIO_InitTypeDef gpio_initstructure = {0};

        gpio_initstructure.Alternate = GPIO_AF5_SPI2;
        gpio_initstructure.Mode = GPIO_MODE_AF_PP;
        gpio_initstructure.Pin = GPIO_PIN_6;
        gpio_initstructure.Pull = GPIO_NOPULL;
        gpio_initstructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(GPIOC, &gpio_initstructure);

        gpio_initstructure.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
        HAL_GPIO_Init(GPIOB, &gpio_initstructure);

        HAL_NVIC_SetPriority(SPI2_IRQn, 14, 14);
        HAL_NVIC_EnableIRQ(SPI2_IRQn);

        i2s_dmatx.Instance = DMA1_Stream4;
        i2s_dmatx.Init.Channel = DMA_CHANNEL_0;
        i2s_dmatx.Init.Direction = DMA_MEMORY_TO_PERIPH;
        i2s_dmatx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        i2s_dmatx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
        i2s_dmatx.Init.MemBurst = DMA_MBURST_SINGLE;
        i2s_dmatx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        i2s_dmatx.Init.MemInc = DMA_MINC_ENABLE;
        i2s_dmatx.Init.Mode = DMA_NORMAL;   
        i2s_dmatx.Init.PeriphBurst = DMA_PBURST_SINGLE;
        i2s_dmatx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        i2s_dmatx.Init.PeriphInc = DMA_PINC_DISABLE;
        i2s_dmatx.Init.Priority = DMA_PRIORITY_HIGH;

        if (HAL_DMA_Init(&i2s_dmatx) != HAL_OK)
        {
            Error_Handler();
        }

        i2s_dmarx.Instance = DMA1_Stream3;
        i2s_dmarx.Init.Channel = DMA_CHANNEL_0;
        i2s_dmarx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        i2s_dmarx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        i2s_dmarx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
        i2s_dmarx.Init.MemBurst = DMA_MBURST_SINGLE;
        i2s_dmarx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        i2s_dmarx.Init.MemInc = DMA_MINC_ENABLE;
        i2s_dmarx.Init.Mode = DMA_NORMAL;
        i2s_dmarx.Init.PeriphBurst = DMA_PBURST_SINGLE;
        i2s_dmarx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        i2s_dmarx.Init.PeriphInc = DMA_PINC_DISABLE;
        i2s_dmarx.Init.Priority = DMA_PRIORITY_HIGH;

        if (HAL_DMA_Init(&i2s_dmarx) != HAL_OK)
        {
            Error_Handler();
        }

        __HAL_LINKDMA(hi2s, hdmatx, i2s_dmatx);
        __HAL_LINKDMA(hi2s, hdmarx, i2s_dmarx);

        HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 14, 14);
        HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);

        HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 14, 14);
        HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
    }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef *hi2s)
{
    __nop();
}

char i2s_set_freq(uint32_t hz)
{
    HAL_I2S_DMAStop(&i2s2_handle);
    HAL_RCCEx_DisablePLLI2S();

    switch (hz)
    {
    case 8000:
        plli2s.PLLI2SN = 256;
        plli2s.PLLI2SR = 5;
        i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_8K;
        break;
    
    case 16000:
        plli2s.PLLI2SN = 426;
        plli2s.PLLI2SR = 4;
        i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_16K;
        break;

    case 32000:
        plli2s.PLLI2SN = 426;
        plli2s.PLLI2SR = 4;
        i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_32K;
        break;

    case 44100:
        plli2s.PLLI2SN = 406;
        plli2s.PLLI2SR = 3;
        i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_44K;
        break;

    case 48000:
        plli2s.PLLI2SN = 258;
        plli2s.PLLI2SR = 3;
        i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_48K;
        break;

    case 96000:
        plli2s.PLLI2SN = 246;
        plli2s.PLLI2SR = 2;
        i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_96K;
        break;

    case 22050:
        plli2s.PLLI2SN = 406;
        plli2s.PLLI2SR = 3;
        i2s2_handle.Init.AudioFreq = I2S_AUDIOFREQ_22K;
        break;
    
    default:
        return 1;
    }

    if (HAL_RCCEx_EnablePLLI2S(&plli2s) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_I2S_Init(&i2s2_handle) != HAL_OK)
    {
        Error_Handler();
    }

    return 0;
}
