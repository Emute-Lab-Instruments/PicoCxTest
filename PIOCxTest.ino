#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "stream_tx.pio.h"
#include "stream_rx.pio.h"
#include "streamMessaging.hpp"


constexpr size_t TX_DATA_PIN = 2;
constexpr size_t TX_FRAME_PIN = 3;
constexpr size_t RX_DATA_PIN = 5;
constexpr size_t RX_FRAME_PIN = 6;
constexpr float BIT_RATE = 20000000.0f;

#define DMA_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#define PIO_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY

static uint dma_channel_tx;
static uint32_t read_size;



PIO pioTx;
uint smTx;
uint offsetTx;

dma_channel_config config_tx;


// DMA interrupt handler, called when a DMA channel has transmitted its data
static void __not_in_flash_func(dma_irq_handler_tx)() {
    if (dma_channel_tx >= 0 && dma_irqn_get_channel_status(0, dma_channel_tx)) {
        dma_irqn_acknowledge_channel(0, dma_channel_tx);
        // Serial.printf("dma_tx done\n");
    }
}


void setup() 
{
	Serial.begin();
    while(!Serial) {}
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&stream_tx_program, &pioTx, &smTx, &offsetTx, TX_DATA_PIN, 2, true);
    stream_tx_program_init(pioTx, smTx, offsetTx, TX_DATA_PIN, TX_FRAME_PIN, BIT_RATE);
    Serial.printf("Tx prog: %d\n", success);

    irq_add_shared_handler(dma_get_irq_num(0), dma_irq_handler_tx, DMA_IRQ_PRIORITY);
    irq_set_enabled(dma_get_irq_num(0), true);

    // setup dma for write
    dma_channel_tx = dma_claim_unused_channel(false);
    if (dma_channel_tx < 0) {
        panic("No free dma channels");
    }
    config_tx = dma_channel_get_default_config(dma_channel_tx);
    channel_config_set_transfer_data_size(&config_tx, DMA_SIZE_32);
    channel_config_set_read_increment(&config_tx, true);
    channel_config_set_write_increment(&config_tx, false);
    // enable irq for tx
    dma_irqn_set_channel_enabled(0, dma_channel_tx, true);        

    //connect DMA with PIO Rx
    channel_config_set_dreq(&config_tx, pio_get_dreq(pioTx, smTx, true));
}


float vf=0.0;
size_t vu = 0;

void loop() {
    streamMessaging::msgpacket m;
    vu = rand();
    streamMessaging::createMessage(m, vu, streamMessaging::messageTypes::CTRL);
    // vf += 0.1f;
    dma_channel_configure(dma_channel_tx, &config_tx, &pioTx->txf[smTx], &m, 2, true); // dma started    
	sleep_us(1000000/30000);
}

PIO pioRx;
uint smRx;
uint offsetRx;
#define RX_BUFFER_SIZE 64
#define RX_BUFFER_SIZE_WORDS RX_BUFFER_SIZE / 4
uint8_t rx_buffer_a[RX_BUFFER_SIZE] __attribute__((aligned(RX_BUFFER_SIZE)));
uint8_t rx_buffer_b[RX_BUFFER_SIZE] __attribute__((aligned(RX_BUFFER_SIZE)));

size_t* rx_buffer_a_word = (size_t*)rx_buffer_a;
size_t* rx_buffer_b_word = (size_t*)rx_buffer_b;

volatile uint32_t last_dma_pos = 0;

size_t * curr_rx_buffer;

static uint dma_channel_rx_a, dma_channel_rx_b, current_rx_dma;
dma_channel_config config_rx_a,  config_rx_b;



void setup1() {
    while(!Serial) {}
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&stream_rx_program, &pioRx, &smRx, &offsetRx, RX_DATA_PIN, 2, true);

	stream_rx_program_init(pioRx, smRx, offsetRx, RX_DATA_PIN, BIT_RATE);
    Serial.printf("Rx prog: %d\n", success);

    dma_channel_rx_a = dma_claim_unused_channel(false);
    if (dma_channel_rx_a < 0) {
        panic("No free dma channels");
    }
    dma_channel_rx_b = dma_claim_unused_channel(false);
    if (dma_channel_rx_b < 0) {
        panic("No free dma channels");
    }

    config_rx_a = dma_channel_get_default_config(dma_channel_rx_a);
    channel_config_set_transfer_data_size(&config_rx_a, DMA_SIZE_32);
    channel_config_set_read_increment(&config_rx_a, false);
    channel_config_set_write_increment(&config_rx_a, true);
    channel_config_set_ring(&config_rx_a, true, __builtin_ctz(RX_BUFFER_SIZE));    
    // read_size = 1;

    channel_config_set_dreq(&config_rx_a, pio_get_dreq(pioRx, smRx, false));

    channel_config_set_chain_to(&config_rx_a, dma_channel_rx_b); 
    dma_channel_configure(dma_channel_rx_a, &config_rx_a, 
                         rx_buffer_a, 
                         &pioRx->rxf[smRx], 
                         RX_BUFFER_SIZE_WORDS, false);    


    //channel B
    config_rx_b = dma_channel_get_default_config(dma_channel_rx_b);
    channel_config_set_transfer_data_size(&config_rx_b, DMA_SIZE_32);
    channel_config_set_read_increment(&config_rx_b, false);
    channel_config_set_write_increment(&config_rx_b, true);
    channel_config_set_ring(&config_rx_b, true, __builtin_ctz(RX_BUFFER_SIZE));    

    channel_config_set_dreq(&config_rx_b, pio_get_dreq(pioRx, smRx, false));

    channel_config_set_chain_to(&config_rx_b, dma_channel_rx_a); 
    dma_channel_configure(dma_channel_rx_b, &config_rx_b, 
                         rx_buffer_b, 
                         &pioRx->rxf[smRx], 
                         RX_BUFFER_SIZE_WORDS, false);    
                         
    dma_channel_set_config(dma_channel_rx_a, &config_rx_a, false);
    dma_channel_set_config(dma_channel_rx_b, &config_rx_b, false);

    current_rx_dma = dma_channel_rx_a;  
    curr_rx_buffer = rx_buffer_a_word;

    dma_channel_start(dma_channel_rx_a);

}
uint8_t lastRead=-1;
size_t counter=0;
const size_t checkevery=15000;
size_t errorCount=0;
size_t totalMessagesReceived=0;
void loop1() {
    uint32_t remaining = dma_channel_hw_addr(current_rx_dma)->transfer_count;
    uint32_t current_dma_pos = (RX_BUFFER_SIZE_WORDS - remaining);

    
        // Process new data
    while (current_dma_pos - last_dma_pos == 2) {
        streamMessaging::msgpacket *msg = reinterpret_cast<streamMessaging::msgpacket*>(&curr_rx_buffer[last_dma_pos]);
        if (!streamMessaging::checksumIsOk(msg) || !streamMessaging::magicByteOk(msg)) {
            errorCount++;
        }
        totalMessagesReceived++;
        last_dma_pos = last_dma_pos + 2;
        if (counter++ == checkevery) {
            Serial.printf("%d messages received, %d errors, %d total\n", checkevery, errorCount,totalMessagesReceived);
            counter=0;
            errorCount=0;
        }
        // Serial.printf("%d %d %d %d\n", current_rx_dma, remaining, current_dma_pos, last_dma_pos);
        if (!remaining) {
            //move to the other channel
            current_rx_dma = current_rx_dma == dma_channel_rx_a ? dma_channel_rx_b : dma_channel_rx_a;
            curr_rx_buffer = current_rx_dma == dma_channel_rx_a ? rx_buffer_a_word : rx_buffer_b_word;
            last_dma_pos = 0;
        }
    }


    // delay(5);
}
