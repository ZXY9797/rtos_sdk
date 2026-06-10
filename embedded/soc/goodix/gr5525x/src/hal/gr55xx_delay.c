#include "gr55xx_delay.h"
#include "gr55xx_hal.h"

void delay_us(uint32_t number_of_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = number_of_us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

void delay_ms(uint32_t number_of_ms)
{
    while (number_of_ms--)
    {
        delay_us(1000);
    }
}
