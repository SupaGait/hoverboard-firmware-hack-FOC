/**
 * Tiny UART IAP bootloader for CarrierHasArrived board A (USART2 PA2/PA3).
 * Lives at 0x08000000 (16 KiB). App at 0x08004000.
 */
#include <stdint.h>
#include <string.h>
#include "iap_protocol.h"

#define RCC_APB2ENR   (*(volatile uint32_t *)0x40021018u)
#define RCC_APB1ENR   (*(volatile uint32_t *)0x4002101Cu)
#define GPIOA_CRL     (*(volatile uint32_t *)0x40010800u)
#define USART2_SR     (*(volatile uint32_t *)0x40004400u)
#define USART2_DR     (*(volatile uint32_t *)0x40004404u)
#define USART2_BRR    (*(volatile uint32_t *)0x40004408u)
#define USART2_CR1    (*(volatile uint32_t *)0x4000440Cu)

#define FLASH_ACR     (*(volatile uint32_t *)0x40022000u)
#define FLASH_KEYR    (*(volatile uint32_t *)0x40022004u)
#define FLASH_SR      (*(volatile uint32_t *)0x4002200Cu)
#define FLASH_CR      (*(volatile uint32_t *)0x40022010u)
#define FLASH_AR      (*(volatile uint32_t *)0x40022014u)

#define FLASH_SR_BSY  (1u << 0)
#define FLASH_CR_PG   (1u << 0)
#define FLASH_CR_PER  (1u << 1)
#define FLASH_CR_STRT (1u << 6)
#define FLASH_CR_LOCK (1u << 7)
#define FLASH_KEY1    0x45670123u
#define FLASH_KEY2    0xCDEF89ABu
#define FLASH_PAGE    0x800u

#define USART_SR_RXNE (1u << 5)
#define USART_SR_TXE  (1u << 7)
#define USART_CR1_TE  (1u << 3)
#define USART_CR1_RE  (1u << 2)
#define USART_CR1_UE  (1u << 13)

#define IAP_IMAGE_MAX  (IAP_APP_MAX_SIZE - 8u)

enum {
  ST_IDLE = 0,
  ST_READY,
  ST_PROGRAM,
};

static uint8_t g_rx[IAP_MAX_FRAME];
static uint16_t g_rx_n;
static uint8_t g_prev;
static uint8_t g_sync;
static uint8_t g_state;
static uint32_t g_img_size;
static uint32_t g_img_crc;
static uint32_t g_written;
static uint8_t g_expect_seq;
static uint32_t g_run_crc;

static void uart_init(void)
{
  RCC_APB2ENR |= (1u << 2);  /* IOPAEN */
  RCC_APB1ENR |= (1u << 17); /* USART2EN */
  /* PA2 AF_PP 50MHz, PA3 input pull-up: CNF/MODE in CRL */
  uint32_t crl = GPIOA_CRL;
  crl &= ~0x0000FF00u;
  crl |= 0x00004B00u; /* PA2=0xB AF_PP 50MHz, PA3=0x4 floating input → use pull via ODR */
  GPIOA_CRL = crl;
  (*(volatile uint32_t *)0x4001080Cu) |= (1u << 3); /* GPIOA_ODR pull-up on PA3 */

  /* 8 MHz HSI / 115200 ≈ 69.444 → mantissa 69, frac 7 → BRR 0x457 */
  USART2_BRR = 0x457u;
  USART2_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart_putc(uint8_t c)
{
  while ((USART2_SR & USART_SR_TXE) == 0) {
  }
  USART2_DR = c;
}

static int uart_getc(uint8_t *c)
{
  if ((USART2_SR & USART_SR_RXNE) == 0) {
    return 0;
  }
  *c = (uint8_t)USART2_DR;
  return 1;
}

static void flash_unlock(void)
{
  if (FLASH_CR & FLASH_CR_LOCK) {
    FLASH_KEYR = FLASH_KEY1;
    FLASH_KEYR = FLASH_KEY2;
  }
}

static void flash_lock(void)
{
  FLASH_CR |= FLASH_CR_LOCK;
}

static void flash_wait(void)
{
  while (FLASH_SR & FLASH_SR_BSY) {
  }
}

static int flash_erase_page(uint32_t addr)
{
  flash_wait();
  FLASH_CR |= FLASH_CR_PER;
  FLASH_AR = addr;
  FLASH_CR |= FLASH_CR_STRT;
  flash_wait();
  FLASH_CR &= ~FLASH_CR_PER;
  return 0;
}

static int flash_program_halfword(uint32_t addr, uint16_t data)
{
  flash_wait();
  FLASH_CR |= FLASH_CR_PG;
  *(volatile uint16_t *)addr = data;
  flash_wait();
  FLASH_CR &= ~FLASH_CR_PG;
  return (*(volatile uint16_t *)addr == data) ? 0 : -1;
}

static int erase_app_region(void)
{
  flash_unlock();
  for (uint32_t a = IAP_APP_BASE; a < IAP_APP_BASE + IAP_APP_MAX_SIZE; a += FLASH_PAGE) {
    if (flash_erase_page(a) != 0) {
      flash_lock();
      return -1;
    }
  }
  flash_lock();
  return 0;
}

static int program_bytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
  flash_unlock();
  uint32_t i = 0;
  while (i < len) {
    uint16_t hw;
    if (i + 1 < len) {
      hw = (uint16_t)data[i] | ((uint16_t)data[i + 1] << 8);
      i += 2;
    } else {
      hw = (uint16_t)data[i] | 0xFF00u;
      i += 1;
    }
    if (flash_program_halfword(addr, hw) != 0) {
      flash_lock();
      return -1;
    }
    addr += 2;
  }
  flash_lock();
  return 0;
}

static void send_frame(uint8_t msg, uint8_t seq, const uint8_t *payload, uint16_t len)
{
  uint8_t buf[IAP_MAX_FRAME];
  buf[0] = (uint8_t)(IAP_START_FRAME & 0xFF);
  buf[1] = (uint8_t)(IAP_START_FRAME >> 8);
  buf[2] = msg;
  buf[3] = seq;
  buf[4] = (uint8_t)(len & 0xFF);
  buf[5] = (uint8_t)(len >> 8);
  if (len && payload) {
    memcpy(&buf[6], payload, len);
  }
  uint32_t crc = iap_crc32(buf, IAP_HEADER_LEN + len);
  buf[IAP_HEADER_LEN + len + 0] = (uint8_t)(crc);
  buf[IAP_HEADER_LEN + len + 1] = (uint8_t)(crc >> 8);
  buf[IAP_HEADER_LEN + len + 2] = (uint8_t)(crc >> 16);
  buf[IAP_HEADER_LEN + len + 3] = (uint8_t)(crc >> 24);
  uint16_t total = (uint16_t)(IAP_HEADER_LEN + len + IAP_CRC_LEN);
  for (uint16_t i = 0; i < total; i++) {
    uart_putc(buf[i]);
  }
}

static void send_ack(uint8_t seq)
{
  send_frame(IAP_MSG_ACK, seq, 0, 0);
}

static void send_nack(uint8_t seq, uint8_t code)
{
  send_frame(IAP_MSG_NACK, seq, &code, 1);
}

static void send_done(uint8_t seq)
{
  send_frame(IAP_MSG_DONE, seq, 0, 0);
}

static int app_valid(void)
{
  uint32_t msp = *(volatile uint32_t *)IAP_APP_BASE;
  uint32_t rst = *(volatile uint32_t *)(IAP_APP_BASE + 4u);
  /* ST-Link programs the image only. The CRC trailer at the end of the region
   * is left untouched, so a sane vector table is what decides the app can run. */
  if (msp < 0x20000000u || msp > 0x2000C000u) {
    return 0;
  }
  if ((rst & 1u) == 0u) {
    return 0;
  }
  if (rst < IAP_APP_BASE || rst >= IAP_APP_BASE + IAP_APP_MAX_SIZE) {
    return 0;
  }
  return 1;
}

static void jump_to_app(void)
{
  uint32_t msp = *(volatile uint32_t *)IAP_APP_BASE;
  uint32_t rv = *(volatile uint32_t *)(IAP_APP_BASE + 4u);

  __asm volatile("cpsid i" ::: "memory");
  *((volatile uint32_t *)0xE000E010u) = 0u; /* SysTick->CTRL */
  *((volatile uint32_t *)0xE000E018u) = 0u; /* SysTick->VAL */
  for (uint32_t i = 0; i < 8u; i++) {
    *((volatile uint32_t *)(0xE000E180u + 4u * i)) = 0xFFFFFFFFu; /* NVIC->ICER */
    *((volatile uint32_t *)(0xE000E280u + 4u * i)) = 0xFFFFFFFFu; /* NVIC->ICPR */
  }
  *((volatile uint32_t *)0xE000ED08u) = IAP_APP_BASE; /* VTOR */
  __asm volatile(
      "dsb\n"
      "isb\n"
      "msr msp, %0\n"
      "dsb\n"
      "isb\n"
      "cpsie i\n"
      "bx %1\n"
      :
      : "r"(msp), "r"(rv)
      : "memory");
  while (1) {
  }
}

static void handle_frame(uint8_t msg, uint8_t seq, const uint8_t *payload, uint16_t len)
{
  switch (msg) {
  case IAP_MSG_HELLO:
    g_state = ST_READY;
    g_written = 0;
    g_expect_seq = 0;
    send_ack(seq);
    break;
  case IAP_MSG_BEGIN:
    if (g_state < ST_READY || len < 9) {
      send_nack(seq, IAP_NACK_STATE);
      return;
    }
    g_img_size = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                 ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
    g_img_crc = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8) |
                ((uint32_t)payload[6] << 16) | ((uint32_t)payload[7] << 24);
    if (payload[8] != IAP_ROLE_SELF) {
      send_nack(seq, IAP_NACK_ROLE);
      return;
    }
    if (g_img_size == 0 || g_img_size > IAP_IMAGE_MAX) {
      send_nack(seq, IAP_NACK_TOO_LARGE);
      return;
    }
    if (erase_app_region() != 0) {
      send_nack(seq, IAP_NACK_FLASH);
      return;
    }
    g_written = 0;
    g_expect_seq = 0;
    g_run_crc = 0xFFFFFFFFu;
    g_state = ST_PROGRAM;
    send_ack(seq);
    break;
  case IAP_MSG_DATA:
    if (g_state != ST_PROGRAM) {
      send_nack(seq, IAP_NACK_STATE);
      return;
    }
    if (seq != g_expect_seq) {
      send_nack(seq, IAP_NACK_SEQ);
      return;
    }
    if (g_written + len > g_img_size) {
      send_nack(seq, IAP_NACK_TOO_LARGE);
      return;
    }
    if (program_bytes(IAP_APP_BASE + g_written, payload, len) != 0) {
      send_nack(seq, IAP_NACK_FLASH);
      return;
    }
    for (uint16_t i = 0; i < len; i++) {
      g_run_crc ^= payload[i];
      for (int b = 0; b < 8; b++) {
        uint32_t mask = -(g_run_crc & 1u);
        g_run_crc = (g_run_crc >> 1) ^ (0xEDB88320u & mask);
      }
    }
    g_written += len;
    g_expect_seq++;
    send_ack(seq);
    break;
  case IAP_MSG_COMMIT:
    if (g_state != ST_PROGRAM || g_written != g_img_size) {
      send_nack(seq, IAP_NACK_STATE);
      return;
    }
    g_run_crc = ~g_run_crc;
    if (g_run_crc != g_img_crc) {
      send_nack(seq, IAP_NACK_IMAGE_CRC);
      return;
    }
    {
      uint8_t meta[8];
      meta[0] = (uint8_t)(g_img_size);
      meta[1] = (uint8_t)(g_img_size >> 8);
      meta[2] = (uint8_t)(g_img_size >> 16);
      meta[3] = (uint8_t)(g_img_size >> 24);
      meta[4] = (uint8_t)(g_img_crc);
      meta[5] = (uint8_t)(g_img_crc >> 8);
      meta[6] = (uint8_t)(g_img_crc >> 16);
      meta[7] = (uint8_t)(g_img_crc >> 24);
      if (program_bytes(IAP_APP_META_ADDR, meta, 8) != 0) {
        send_nack(seq, IAP_NACK_FLASH);
        return;
      }
    }
    send_done(seq);
    for (volatile uint32_t d = 0; d < 100000u; d++) {
    }
    jump_to_app();
    break;
  case IAP_MSG_ABORT:
    g_state = ST_READY;
    send_ack(seq);
    break;
  default:
    send_nack(seq, IAP_NACK_STATE);
    break;
  }
}

static void feed_byte(uint8_t byte)
{
  if (!g_sync) {
    uint16_t start = ((uint16_t)byte << 8) | g_prev;
    g_prev = byte;
    if (start == IAP_START_FRAME) {
      g_rx[0] = (uint8_t)(IAP_START_FRAME & 0xFF);
      g_rx[1] = (uint8_t)(IAP_START_FRAME >> 8);
      g_rx_n = 2;
      g_sync = 1;
    }
    return;
  }
  if (g_rx_n < IAP_MAX_FRAME) {
    g_rx[g_rx_n++] = byte;
  } else {
    g_sync = 0;
    g_rx_n = 0;
    g_prev = byte;
    return;
  }
  g_prev = byte;
  if (g_rx_n < IAP_HEADER_LEN) {
    return;
  }
  uint16_t plen = (uint16_t)g_rx[4] | ((uint16_t)g_rx[5] << 8);
  if (plen > IAP_MAX_PAYLOAD) {
    g_sync = 0;
    g_rx_n = 0;
    return;
  }
  uint16_t total = (uint16_t)(IAP_HEADER_LEN + plen + IAP_CRC_LEN);
  if (g_rx_n < total) {
    return;
  }
  uint32_t got = (uint32_t)g_rx[total - 4] | ((uint32_t)g_rx[total - 3] << 8) |
                 ((uint32_t)g_rx[total - 2] << 16) | ((uint32_t)g_rx[total - 1] << 24);
  uint32_t expect = iap_crc32(g_rx, IAP_HEADER_LEN + plen);
  uint8_t msg = g_rx[2];
  uint8_t seq = g_rx[3];
  g_sync = 0;
  g_rx_n = 0;
  if (got != expect) {
    send_nack(seq, IAP_NACK_BAD_CRC);
    return;
  }
  handle_frame(msg, seq, &g_rx[IAP_HEADER_LEN], plen);
}

/* Exported entry for the running app to vector into IAP without a full chip reset. */
__attribute__((section(".iap_entry"), used, noinline)) void iap_run(void)
{
  uart_init();
  g_state = ST_READY;
  g_sync = 0;
  g_rx_n = 0;
  for (;;) {
    uint8_t c;
    if (uart_getc(&c)) {
      feed_byte(c);
    }
  }
}

int main(void)
{
  uart_init();
  g_state = ST_IDLE;
  g_sync = 0;
  g_rx_n = 0;

  /* Brief listen window, then boot app if valid. */
  for (uint32_t i = 0; i < 400000u; i++) {
    uint8_t c;
    if (uart_getc(&c)) {
      feed_byte(c);
      /* Stay in IAP once HELLO moved state. */
      if (g_state != ST_IDLE) {
        break;
      }
    }
  }
  if (g_state == ST_IDLE && app_valid()) {
    jump_to_app();
  }
  g_state = ST_READY;
  for (;;) {
    uint8_t c;
    if (uart_getc(&c)) {
      feed_byte(c);
    }
  }
}
