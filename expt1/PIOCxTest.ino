// Test PIO with manual blocking writes - NO DMA
#include <hardware/pio.h>
#include <hardware/gpio.h>
#include <hardware/structs/sio.h>
#include "piocx_tx.pio.h"
#include "piocx_rx.pio.h"

constexpr size_t TX_DATA_PIN = 0;
constexpr size_t TX_FRAME_PIN = 2;
constexpr size_t RX_DATA_PIN = 3;
constexpr size_t RX_FRAME_PIN = 5;
constexpr float BIT_RATE = 4000000.0f;

// Simple test message (6 bytes)
uint32_t test_message[6] = {
  0x000000AA,  // Byte 0: 0xAA (10101010)
  0x00000055,  // Byte 1: 0x55 (01010101)
  0x000000FF,  // Byte 2: 0xFF (11111111)
  0x00000000,  // Byte 3: 0x00 (00000000)
  0x000000F0,  // Byte 4: 0xF0 (11110000)
  0x0000000F   // Byte 5: 0x0F (00001111)
};

// TX side (Core 0)
PIO tx_pio = pio0;
uint tx_sm;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  delay(500);

  Serial.println("\n=== Manual PIO Test (No DMA) ===");
  Serial.println("Testing PIO with blocking writes\n");

  // Setup TX PIO
  uint offset = pio_add_program(tx_pio, &piocx_tx_program);
  tx_sm = pio_claim_unused_sm(tx_pio, true);

  // Slow clock for debugging
  float clk_div = clock_get_hz(clk_sys) / 1000.0f; // 1 kHz

  piocx_tx_program_init(tx_pio, tx_sm, offset, TX_DATA_PIN, TX_FRAME_PIN, clk_div);

  Serial.println("TX PIO initialized");
  Serial.print("Clock divider: ");
  Serial.println(clk_div);

  // Debug: Print PINCTRL register
  Serial.print("PINCTRL register: 0x");
  Serial.println(tx_pio->sm[tx_sm].pinctrl, HEX);
  Serial.print("  OUT_BASE: ");
  Serial.println((tx_pio->sm[tx_sm].pinctrl >> 0) & 0x1F);
  Serial.print("  OUT_COUNT: ");
  Serial.println((tx_pio->sm[tx_sm].pinctrl >> 20) & 0x3F);
  Serial.print("  SET_BASE: ");
  Serial.println((tx_pio->sm[tx_sm].pinctrl >> 5) & 0x1F);
  Serial.print("  SET_COUNT: ");
  Serial.println((tx_pio->sm[tx_sm].pinctrl >> 26) & 0x7);

  Serial.println("Test message (6 bytes): AA 55 FF 00 F0 0F\n");
}

uint32_t msg_count = 0;

void loop() {
  Serial.print("Sending message #");
  Serial.println(++msg_count);

  // Check FIFO before sending
  Serial.print("  TX FIFO level before: ");
  Serial.println(pio_sm_get_tx_fifo_level(tx_pio, tx_sm));

  // Manually push 6 bytes to PIO FIFO
  for (int i = 0; i < 6; i++) {
    pio_sm_put_blocking(tx_pio, tx_sm, test_message[i]);
    Serial.print("  Pushed byte ");
    Serial.print(i);
    Serial.print(": 0x");
    Serial.print((uint8_t)test_message[i], HEX);
    Serial.print(" (32-bit: 0x");
    Serial.print(test_message[i], HEX);
    Serial.println(")");
  }

  // Check FIFO after sending
  Serial.print("  TX FIFO level after push: ");
  Serial.println(pio_sm_get_tx_fifo_level(tx_pio, tx_sm));

  // Wait for FIFO to drain
  Serial.print("  Waiting for TX FIFO to drain...");
  uint32_t wait_start = millis();
  while (!pio_sm_is_tx_fifo_empty(tx_pio, tx_sm) && (millis() - wait_start) < 1000) {
    delay(10);
  }
  Serial.println("done");

  Serial.print("  TX FIFO level after drain: ");
  Serial.println(pio_sm_get_tx_fifo_level(tx_pio, tx_sm));

  Serial.print("  GPIO states: Frame=");
  Serial.print((sio_hw->gpio_in >> TX_FRAME_PIN) & 1);
  Serial.print(" Data=");
  Serial.println((sio_hw->gpio_in >> TX_DATA_PIN) & 1);

  Serial.println("Message sent!\n");
  delay(2000); // 2 seconds between messages
}

// RX side (Core 1)
PIO rx_pio = pio1;
uint rx_sm;
uint8_t rx_buffer[6];
volatile size_t rx_byte_count = 0;
volatile bool rx_packet_ready = false;
volatile uint32_t rx_isr_count = 0; // Count ISR calls

void __isr pio_rx_handler() {
  uint32_t isr_count = rx_isr_count;
  isr_count++;
  rx_isr_count = isr_count;

  if (!pio_sm_is_rx_fifo_empty(rx_pio, rx_sm)) {
    size_t count = rx_byte_count;
    uint32_t raw_data = pio_sm_get(rx_pio, rx_sm);
    rx_buffer[count] = raw_data & 0xFF;
    count++;

    if (count >= 6) {
      rx_packet_ready = true;
      rx_byte_count = 0;
    } else {
      rx_byte_count = count;
    }
  }
  irq_clear(PIO1_IRQ_0);
}

void setup1() {
  delay(1000);
  Serial.println("=== CORE 1: RX Setup ===\n");

  // Setup RX PIO
  uint offset = pio_add_program(rx_pio, &piocx_rx_program);
  rx_sm = pio_claim_unused_sm(rx_pio, true);

  Serial.print("RX PIO program offset: ");
  Serial.println(offset);
  Serial.print("RX state machine: ");
  Serial.println(rx_sm);

  float clk_div = clock_get_hz(clk_sys) / 1000.0f;
  piocx_rx_program_init(rx_pio, rx_sm, offset, RX_DATA_PIN, RX_FRAME_PIN, clk_div);

  // Enable interrupt
  pio_interrupt_source_t irq_source = (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + rx_sm);
  pio_set_irq0_source_enabled(rx_pio, irq_source, true);
  irq_set_exclusive_handler(PIO1_IRQ_0, pio_rx_handler);
  irq_set_enabled(PIO1_IRQ_0, true);

  Serial.print("RX interrupt source: ");
  Serial.println(irq_source);
  Serial.println("RX PIO initialized\n");
}

uint32_t last_gpio_check = 0;
uint32_t last_isr_count = 0;

void loop1() {
  if (rx_packet_ready) {
    Serial.println("*** RX RECEIVED MESSAGE ***");
    Serial.print("Data: ");
    for (int i = 0; i < 6; i++) {
      if (rx_buffer[i] < 0x10) Serial.print("0");
      Serial.print(rx_buffer[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    // Check if it matches
    bool match = true;
    for (int i = 0; i < 6; i++) {
      if (rx_buffer[i] != (uint8_t)test_message[i]) {
        match = false;
        break;
      }
    }
    Serial.println(match ? "MATCH!" : "MISMATCH!");
    Serial.println();

    rx_packet_ready = false;
  }

  // Periodically check GPIO state and RX status
  if (millis() - last_gpio_check > 500) {
    last_gpio_check = millis();

    Serial.println("--- RX Status ---");
    Serial.print("  ISR count: ");
    Serial.print(rx_isr_count);
    Serial.print(" (delta: ");
    Serial.print(rx_isr_count - last_isr_count);
    Serial.println(")");
    last_isr_count = rx_isr_count;

    Serial.print("  RX FIFO level: ");
    Serial.println(pio_sm_get_rx_fifo_level(rx_pio, rx_sm));

    Serial.print("  RX FIFO empty: ");
    Serial.println(pio_sm_is_rx_fifo_empty(rx_pio, rx_sm) ? "YES" : "NO");

    Serial.print("  Partial byte count: ");
    Serial.println(rx_byte_count);

    Serial.print("  GPIO - Frame: ");
    Serial.print((sio_hw->gpio_in >> RX_FRAME_PIN) & 1);
    Serial.print(" Data: ");
    Serial.println((sio_hw->gpio_in >> RX_DATA_PIN) & 1);
    Serial.println();
  }

  delay(10);
}
