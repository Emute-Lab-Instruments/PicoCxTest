#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "uart_tx.pio.h"
#include "uart_rx.pio.h"

enum messageTypes {WAVELEN0, BANK0, BANK1, CTRL,
                CTRL0, CTRL1, CTRL2, CTRL3, CTRL4, CTRL5, DETUNE, OCTSPREAD,
                METAMOD3, METAMOD4, METAMOD5, METAMOD6, METAMOD7, METAMOD8};

struct msg {
    float value;
    uint8_t msgType;
    const uint8_t magicByte = 0x77;
    uint16_t checksum;
};

constexpr size_t TX_DATA_PIN = 0;
constexpr size_t TX_FRAME_PIN = 2;
constexpr size_t RX_DATA_PIN = 3;
constexpr size_t RX_FRAME_PIN = 5;
constexpr float BIT_RATE = 1000000.0f;

#define DMA_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#define PIO_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#define PIO_IRQ_TO_USE 0

// static uint dma_channel_rx;
static uint dma_channel_tx;
static uint32_t read_size;



PIO pioTx;
uint smTx;
uint offsetTx;

dma_channel_config config_tx;

// PIO interrupt handler, called when the state machine fifo is not empty
// note: shouldn't printf in an irq normally!
// static void pio_irq_handler() {
//     dma_channel_hw_t *dma_chan = dma_channel_hw_addr(dma_channel_rx);
//     // Serial.printf("pio_rx dma_rx=%u/%u\n", read_size - dma_chan->transfer_count, read_size);
// }

// DMA interrupt handler, called when a DMA channel has transmitted its data
static void __not_in_flash_func(dma_irq_handler_tx)() {
    if (dma_channel_tx >= 0 && dma_irqn_get_channel_status(0, dma_channel_tx)) {
        dma_irqn_acknowledge_channel(0, dma_channel_tx);
        // Serial.printf("dma_tx done\n");
    }
}

// static void __not_in_flash_func(dma_irq_handler_rx)() {
//     if (dma_channel_rx >= 0 && dma_irqn_get_channel_status(1, dma_channel_rx)) {
//         dma_irqn_acknowledge_channel(1, dma_channel_rx);
//         Serial.printf("dma_rx done\n");
//     }
// }

static void dump_bytes(const char *bptr, uint32_t len) {
    unsigned int i = 0;
    for (i = 0; i < len;) {
        if ((i & 0x0f) == 0) {
            printf("\n");
        } else if ((i & 0x07) == 0) {
            printf(" ");
        }
        printf("%02x ", bptr[i++]);
    }
    printf("\n");
}


uint8_t v=0;


void setup() 
{
	Serial.begin();
    // This will find a free pio and state machine for our program and load it for us
    // We use pio_claim_free_sm_and_add_program_for_gpio_range (for_gpio_range variant)
    // so we will get a PIO instance suitable for addressing gpios >= 32 if needed and supported by the hardware
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&uart_tx_program, &pioTx, &smTx, &offsetTx, TX_DATA_PIN, 1, true);
    uart_tx_program_init(pioTx, smTx, offsetTx, TX_DATA_PIN, BIT_RATE);

    irq_add_shared_handler(dma_get_irq_num(0), dma_irq_handler_tx, DMA_IRQ_PRIORITY);
    irq_set_enabled(dma_get_irq_num(0), true);

    // setup dma for write
    dma_channel_tx = dma_claim_unused_channel(false);
    if (dma_channel_tx < 0) {
        panic("No free dma channels");
    }
    config_tx = dma_channel_get_default_config(dma_channel_tx);
    channel_config_set_transfer_data_size(&config_tx, DMA_SIZE_8);
    channel_config_set_read_increment(&config_tx, true);
    channel_config_set_write_increment(&config_tx, false);
    // enable irq for tx
    dma_irqn_set_channel_enabled(0, dma_channel_tx, true);        

    //connect DMA with PIO Rx
    channel_config_set_dreq(&config_tx, pio_get_dreq(pioTx, smTx, true));
}

void loop() {
	// uart_tx_program_puts(pio, sm, String(v).c_str());
    dma_channel_configure(dma_channel_tx, &config_tx, &pioTx->txf[smTx], &v, sizeof(v), true); // dma started    
    dma_channel_wait_for_finish_blocking(dma_channel_tx);
    v++;
    v = v % 255;
	sleep_ms(100);
}

PIO pioRx;
uint smRx;
uint offsetRx;
#define RX_BUFFER_SIZE 16
uint8_t rx_buffer_a[RX_BUFFER_SIZE] __attribute__((aligned(RX_BUFFER_SIZE)));
uint8_t rx_buffer_b[RX_BUFFER_SIZE] __attribute__((aligned(RX_BUFFER_SIZE)));
volatile uint32_t last_dma_pos = 0;

uint8_t * curr_rx_buffer;

static uint dma_channel_rx_a, dma_channel_rx_b, current_rx_dma;
dma_channel_config config_rx_a,  config_rx_b;


// char buffer_rx[1] = {0};


void setup1() {
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&uart_rx_program, &pioRx, &smRx, &offsetRx, RX_DATA_PIN, 1, true);

	uart_rx_program_init(pioRx, smRx, offsetRx, RX_DATA_PIN, BIT_RATE);

    // irq_add_shared_handler(pio_get_irq_num(pioRx, PIO_IRQ_TO_USE), pio_irq_handler, PIO_IRQ_PRIORITY);
    // pio_set_irqn_source_enabled(pioRx, PIO_IRQ_TO_USE, pio_get_rx_fifo_not_empty_interrupt_source(smRx), true);
    // irq_set_enabled(pio_get_irq_num(pioRx, PIO_IRQ_TO_USE), true);    

    // irq_add_shared_handler(dma_get_irq_num(1), dma_irq_handler_rx, DMA_IRQ_PRIORITY);
    // irq_set_enabled(dma_get_irq_num(1), true);

    // // Setup dma for read
    dma_channel_rx_a = dma_claim_unused_channel(false);
    if (dma_channel_rx_a < 0) {
        panic("No free dma channels");
    }
    dma_channel_rx_b = dma_claim_unused_channel(false);
    if (dma_channel_rx_b < 0) {
        panic("No free dma channels");
    }

    config_rx_a = dma_channel_get_default_config(dma_channel_rx_a);
    channel_config_set_transfer_data_size(&config_rx_a, DMA_SIZE_8);
    channel_config_set_read_increment(&config_rx_a, false);
    channel_config_set_write_increment(&config_rx_a, true);
    channel_config_set_ring(&config_rx_a, true, __builtin_ctz(RX_BUFFER_SIZE));    
    // read_size = 1;

    // enable irq for rx
    // dma_irqn_set_channel_enabled(1, dma_channel_rx_a, true);    
    // setup dma to read from pio fifo
    channel_config_set_dreq(&config_rx_a, pio_get_dreq(pioRx, smRx, false));

    channel_config_set_chain_to(&config_rx_a, dma_channel_rx_b); 
    dma_channel_configure(dma_channel_rx_a, &config_rx_a, 
                         rx_buffer_a, 
                         (io_rw_8*)&pioRx->rxf[smRx] + 3, 
                         RX_BUFFER_SIZE, false);    


    //channel B
    config_rx_b = dma_channel_get_default_config(dma_channel_rx_b);
    channel_config_set_transfer_data_size(&config_rx_b, DMA_SIZE_8);
    channel_config_set_read_increment(&config_rx_b, false);
    channel_config_set_write_increment(&config_rx_b, true);
    channel_config_set_ring(&config_rx_b, true, __builtin_ctz(RX_BUFFER_SIZE));    

    // read_size = 1;

    // enable irq for rx
    // dma_irqn_set_channel_enabled(1, dma_channel_rx_a, true);    
    // setup dma to read from pio fifo
    channel_config_set_dreq(&config_rx_b, pio_get_dreq(pioRx, smRx, false));

    channel_config_set_chain_to(&config_rx_b, dma_channel_rx_a); 
    dma_channel_configure(dma_channel_rx_b, &config_rx_b, 
                         rx_buffer_b, 
                         (io_rw_8*)&pioRx->rxf[smRx] + 3, 
                         RX_BUFFER_SIZE, false);    
                         
    dma_channel_set_config(dma_channel_rx_a, &config_rx_a, false);
    dma_channel_set_config(dma_channel_rx_b, &config_rx_b, false);

    current_rx_dma = dma_channel_rx_a;  
    curr_rx_buffer = rx_buffer_a;

    dma_channel_start(dma_channel_rx_a);

}
uint8_t lastRead=-1;
void loop1() {
    // Serial.printf("%d %d\n", dma_channel_is_busy(dma_channel_rx_a), dma_channel_is_busy(dma_channel_rx_b));
    uint32_t remaining = dma_channel_hw_addr(current_rx_dma)->transfer_count;
    uint32_t current_dma_pos = (RX_BUFFER_SIZE - remaining);

    // Serial.printf("%d %d %d %d\n", current_rx_dma, remaining, current_dma_pos, last_dma_pos);
    
        // Process new data
    while (last_dma_pos < current_dma_pos) {
        uint8_t data = curr_rx_buffer[last_dma_pos];
        last_dma_pos = last_dma_pos + 1;
        // Serial.printf("Received: %d\n", data);
        if (data != lastRead+1) {
            Serial.printf("Error %d %d\n", data, lastRead);
        }
        lastRead=data;
    }

    if (!remaining) {
        //move to the other channel
        current_rx_dma = current_rx_dma == dma_channel_rx_a ? dma_channel_rx_b : dma_channel_rx_a;
        curr_rx_buffer = current_rx_dma == dma_channel_rx_a ? rx_buffer_a : rx_buffer_b;
        last_dma_pos = 0;
    }
    // Print both buffers to see what's actually in them
// Serial.printf("Buffer A: ");
// for(int i=0; i<16; i++) Serial.printf("%d ", rx_buffer_a[i]);
// Serial.printf("\nBuffer B: ");
// for(int i=0; i<16; i++) Serial.printf("%d ", rx_buffer_b[i]);
// Serial.printf("\n");

    delay(20 + random(150));
}
