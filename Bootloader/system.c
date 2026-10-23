#include <stdint.h>

/* Minimal clock: HSI 8 MHz, no PLL. Enough for 115200 UART. */
void SystemInit(void)
{
  /* Leave HSI as SYSCLK. Vector table stays at 0x08000000 in the bootloader. */
  *((volatile uint32_t *)0xE000ED08) = 0x08000000u; /* SCB->VTOR */
}
