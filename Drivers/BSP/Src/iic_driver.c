#include "iic_driver.h"
#include "main.h"

I2C_HandleTypeDef iic1_handle = {0};

void iic1_init(void)
{
    iic1_handle.Instance = I2C1;
    iic1_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    iic1_handle.Init.ClockSpeed = 100000u;
    iic1_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    iic1_handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
    iic1_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    iic1_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    iic1_handle.Init.OwnAddress1 = 0x00;
    iic1_handle.Init.OwnAddress2 = 0x00;

    if (HAL_I2C_Init(&iic1_handle) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        __HAL_RCC_I2C1_CLK_ENABLE();

        GPIO_InitTypeDef gpio_initstructure = {0};

        gpio_initstructure.Alternate = GPIO_AF4_I2C1;
        gpio_initstructure.Mode = GPIO_MODE_AF_OD;
        gpio_initstructure.Pin = GPIO_PIN_8 | GPIO_PIN_9;
        gpio_initstructure.Pull = GPIO_NOPULL;
        gpio_initstructure.Speed = GPIO_SPEED_HIGH;

        HAL_GPIO_Init(GPIOB, &gpio_initstructure);
       
        HAL_NVIC_SetPriority(I2C1_EV_IRQn, 14, 14);
        HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1)
    {
        __nop();
    }
}
