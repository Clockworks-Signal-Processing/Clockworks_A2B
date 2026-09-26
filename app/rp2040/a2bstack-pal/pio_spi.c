/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define RTOS 0

#include "pio_spi.h"
#include "hardware/clocks.h"
#include "pico/binary_info.h"

#if RTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

#define SPI_PIO_USE_DMA 1   // set to 1 to use DMA
#define SPI_PIO_FW_CSN 1    // set to 1 to have FW manage CSn

// SPI for use on AmeNote ProtoZOA Board with A2B Bridge Board
#define PICO_SPI_DATARATE   2000 * 1000 // 2MHz with fly wires
#define PICO_SPI_RX_PIN     13 //8 //GP8 U9.11      //GP19 J13.27 SIGMASTUDIO.5 MISO
#define PICO_SPI_SCK_PIN    15 //10 //GP10 U9.6     //GP20 J13.25 SIGMASTUDIO.7 SCK
#define PICO_SPI_TX_PIN     12 //11 //GP11 U9.14    //GP21 J13.23 SIGMASTUDIO.8 MOSI
#define PICO_SPI_CSN_PIN    14 //GP9 U9.1           //GP22 J13.21 SIGMASTUDIO.9 SS

/**
 * @brief Initialize SPI PIO resources
 * Installs PIO SM and associated resources including establishing for DMA transfer
 * 
 * @param pio       The PIO group to use
 * @param sm        The state machine to use
 * @return true     Successful initializaiton
 * @return false    on initialization failure
 */
bool pio_spi_init_hw(pio_spi_inst_t *spi)
{
#if SPI_PIO_USE_DMA
    // Setup DMA for SPI PIO
    // Setup DMA for Tx and Rx
    // Claim DMA channels - true indicates required which will panic if not available

    // Configure the Tx DMA from buffer to PIO register
    spi->spiTxDMA = dma_claim_unused_channel(true);
    // Set initially to default config
    spi->spiTxDMAConfig = dma_channel_get_default_config(spi->spiTxDMA);
    // Set data transfer size
    channel_config_set_transfer_data_size(&spi->spiTxDMAConfig, DMA_SIZE_8);
    // Configure Tx to increment buffer and write to register
    channel_config_set_read_increment(&spi->spiTxDMAConfig, true);
    channel_config_set_write_increment(&spi->spiTxDMAConfig, false);
    // Set the data request register for SM Tx Register
    channel_config_set_dreq(&spi->spiTxDMAConfig, pio_get_dreq(spi->pio, spi->sm, true));

    // Configure the Rx DMA from PIO register to the buffer
    spi->spiRxDMA = dma_claim_unused_channel(true);
    // Set initially to default config
    spi->spiRxDMAConfig = dma_channel_get_default_config(spi->spiRxDMA);
    // Set data transfer size
    channel_config_set_transfer_data_size(&spi->spiRxDMAConfig, DMA_SIZE_8);
    // Configure Rx to read register and write to increment buffer
    channel_config_set_read_increment(&spi->spiRxDMAConfig, false);
    channel_config_set_write_increment(&spi->spiRxDMAConfig, true);
    // Set the data request register for SM Rx Register
    channel_config_set_dreq(&spi->spiRxDMAConfig, pio_get_dreq(spi->pio, spi->sm, false));
#endif // SPI_PIO_USE_DMA

#if SPI_PIO_FW_CSN
    uint cpha0_prog_offs = pio_add_program(spi->pio, &spi_cpha0_program);
    uint cpha1_prog_offs = pio_add_program(spi->pio, &spi_cpha1_program);
#else //SPI_PIO_FW_CSN
    uint cpha0_prog_offs = pio_add_program(spi->pio, &spi_cpha0_cs_program);
    uint cpha1_prog_offs = pio_add_program(spi->pio, &spi_cpha1_cs_program);
#endif //SPI_PIO_FW_CSN

    //float div = clock_get_hz(clk_sys) / (freq * cycles_per_bit);
    float div = clock_get_hz(clk_sys);
    float den = (PICO_SPI_DATARATE * 4/*sm cycles per bit*/);
    div = div / den;

    uint cpha = 1;
#if SPI_PIO_FW_CSN
    pio_spi_init(spi->pio, spi->sm,
        cpha /*cpha*/ ? cpha1_prog_offs : cpha0_prog_offs,
        8, // nbits
        div, //15.625, // 2MHz 31.25,  // 1 MHz
        cpha,  // cpha
        1,  // cpol
        PICO_SPI_SCK_PIN,
        PICO_SPI_TX_PIN,    // MOSI
        PICO_SPI_RX_PIN     // MISO
    );
#else //SPI_PIO_FW_CSN
    pio_spi_cs_init(spi->pio, spi->sm,
        cpha /*cpha*/ ? cpha1_prog_offs : cpha0_prog_offs,
        8, // nbits
        div, //15.625, // 2MHz 31.25,  // 1 MHz
        cpha,  // cpha
        1,  // cpol
        PICO_SPI_CSN_PIN,
        PICO_SPI_SCK_PIN,
        PICO_SPI_TX_PIN,    // MOSI
        PICO_SPI_RX_PIN     // MISO
    );
#endif //SPI_PIO_FW_CSN

    // Make the 'SPI' pins available to picotool
    bi_decl(bi_4pins_with_names(PICO_SPI_RX_PIN, "SPI RX", PICO_SPI_TX_PIN, "SPI TX", PICO_SPI_SCK_PIN, "SPI SCK", PICO_SPI_CSN_PIN, "SPI CS"));

    return true;
}

// Just 8 bit functions provided here. The PIO program supports any frame size
// 1...32, but the software to do the necessary FIFO shuffling is left as an
// exercise for the reader :)
//
// Likewise we only provide MSB-first here. To do LSB-first, you need to
// - Do shifts when reading from the FIFO, for general case n != 8, 16, 32
// - Do a narrow read at a one halfword or 3 byte offset for n == 16, 8
// in order to get the read data correctly justified. 

void __time_critical_func(pio_spi_write8_blocking)(const pio_spi_inst_t *spi, const uint8_t *src, size_t len) {
#if SPI_PIO_USE_DMA
    static uint8_t tempRead;   // scrap character to read into - we do not care about read data

    // Make sure DMA not busy and if so, wait
    while ( dma_channel_is_busy(spi->spiTxDMA) || dma_channel_is_busy(spi->spiRxDMA))
    {
        #if RTOS
        // Allow other higher and similar priority tasks to run
        vTaskDelay( 0 );
        #endif
    }

    // Setup Rx DMA to not increment for write as temp buffer is single character
    channel_config_set_write_increment(&spi->spiRxDMAConfig, false);

    // Setup Tx DMA to increment in case was changed elsewhere
    channel_config_set_read_increment(&spi->spiTxDMAConfig, true);

    // Config Tx DMA
    dma_channel_configure(
        spi->spiTxDMA,               // the DMA Channel
        &spi->spiTxDMAConfig,        // the configuration structure
        (spi->pio == pio0) ? &pio0_hw->txf[spi->sm] : &pio1_hw->txf[spi->sm],  // address to write
        src,             // the read from address
        len,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Config Rx DMA
    dma_channel_configure(
        spi->spiRxDMA,               // the DMA Channel
        &spi->spiRxDMAConfig,        // the configuration structure
        &tempRead,              // where to write data to
        (spi->pio == pio0) ? &pio0_hw->rxf[spi->sm] : &pio1_hw->rxf[spi->sm],  // address to read
        len,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Start DMA Transfers
    dma_start_channel_mask( ((uint32_t)1 << spi->spiTxDMA) | ((uint32_t)1 << spi->spiRxDMA) );

    // Wait here until RxDMA is done signalling that there is no more data to process in DMA
    while (dma_channel_is_busy(spi->spiRxDMA))
    {
        #if RTOS
        // Allow other higher and similar priority tasks to run
        vTaskDelay( 0 );
        #endif
    }

#else // SPI_PIO_USE_DMA
    size_t tx_remain = len, rx_remain = len;
    // Do 8 bit accesses on FIFO, so that write data is byte-replicated. This
    // gets us the left-justification for free (for MSB-first shift-out)
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];
    while (tx_remain || rx_remain) {
        if (tx_remain && !pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = *src++;
            --tx_remain;
        }
        if (rx_remain && !pio_sm_is_rx_fifo_empty(spi->pio, spi->sm)) {
            (void) *rxfifo;
            --rx_remain;
        }
    }
#endif // !SPI_PIO_USE_DMA
}

void __time_critical_func(pio_spi_read8_blocking)(const pio_spi_inst_t *spi, uint8_t *dst, size_t len) {
#if SPI_PIO_USE_DMA
    uint8_t tempWrite = 0;

    // Make sure DMA not busy and if so, wait
    while ( dma_channel_is_busy(spi->spiTxDMA) || dma_channel_is_busy(spi->spiRxDMA))
    {
        #if RTOS
        // Allow other higher and similar priority tasks to run
        vTaskDelay( 0 );
        #endif
    }

    // Setup Rx DMA to increment through buffer
    channel_config_set_write_increment(&spi->spiRxDMAConfig, true);

    // Setup Tx DMA to not increment as sending null data from single character
    channel_config_set_read_increment(&spi->spiTxDMAConfig, false);

    // Config Tx DMA
    dma_channel_configure(
        spi->spiTxDMA,               // the DMA Channel
        &spi->spiTxDMAConfig,        // the configuration structure
        (spi->pio == pio0) ? &pio0_hw->txf[spi->sm] : &pio1_hw->txf[spi->sm],  // address to write
        &tempWrite,             // the read from address
        len,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Config Rx DMA
    dma_channel_configure(
        spi->spiRxDMA,               // the DMA Channel
        &spi->spiRxDMAConfig,        // the configuration structure
        dst,              // where to write data to
        (spi->pio == pio0) ? &pio0_hw->rxf[spi->sm] : &pio1_hw->rxf[spi->sm],  // address to read
        len,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Start DMA Transfers
    dma_start_channel_mask( ((uint32_t)1 << spi->spiTxDMA) | ((uint32_t)1 << spi->spiRxDMA) );

    // Wait here until RxDMA is done signalling that there is no more data to process in DMA
    while ( dma_channel_is_busy(spi->spiRxDMA))
    {
        #if RTOS
        // Allow other higher and similar priority tasks to run
        vTaskDelay( 0 );
        #endif
    }

#else // SPI_PIO_USE_DMA
    size_t tx_remain = len, rx_remain = len;
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];
    while (tx_remain || rx_remain) {
        if (tx_remain && !pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = 0;
            --tx_remain;
        }
        if (rx_remain && !pio_sm_is_rx_fifo_empty(spi->pio, spi->sm)) {
            *dst++ = *rxfifo;
            --rx_remain;
        }
    }
#endif // !SPI_PIO_USE_DMA
}

void __time_critical_func(pio_spi_write8_read8_blocking)(const pio_spi_inst_t *spi, uint8_t *src, uint8_t *dst,
                                                         size_t len) {
#if SPI_PIO_USE_DMA
    // Make sure DMA not busy and if so, wait
    while ( dma_channel_is_busy(spi->spiTxDMA) || dma_channel_is_busy(spi->spiRxDMA))
    {
        #if RTOS
        // Allow other higher and similar priority tasks to run
        vTaskDelay( 0 );
        #endif
    }

    // Setup Rx DMA to increment through buffer
    channel_config_set_write_increment(&spi->spiRxDMAConfig, true);

    // Setup Tx DMA to increment through buffer
    channel_config_set_read_increment( &spi->spiTxDMAConfig, true);

    // Config Tx DMA
    dma_channel_configure(
        spi->spiTxDMA,               // the DMA Channel
        &spi->spiTxDMAConfig,        // the configuration structure
        (spi->pio == pio0) ? &pio0_hw->txf[spi->sm] : &pio1_hw->txf[spi->sm],  // address to write
        src,             // the read from address
        len,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Config Rx DMA
    dma_channel_configure(
        spi->spiRxDMA,               // the DMA Channel
        &spi->spiRxDMAConfig,        // the configuration structure
        dst,              // where to write data to
        (spi->pio == pio0) ? &pio0_hw->rxf[spi->sm] : &pio1_hw->rxf[spi->sm],  // address to read
        len,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Start DMA Transfers
    dma_start_channel_mask( ((uint32_t)1 << spi->spiTxDMA) | ((uint32_t)1 << spi->spiRxDMA) );

    // Wait here until RxDMA is done signalling that there is no more data to process in DMA
    while ( dma_channel_is_busy(spi->spiRxDMA))
    {
        #if RTOS
        // Allow other higher and similar priority tasks to run
        vTaskDelay( 0 );
        #endif
    }

#else // SPI_PIO_USE_DMA
    size_t tx_remain = len, rx_remain = len;
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    io_rw_8 *rxfifo = (io_rw_8 *) &spi->pio->rxf[spi->sm];
    while (tx_remain || rx_remain) {
        if (tx_remain && !pio_sm_is_tx_fifo_full(spi->pio, spi->sm)) {
            *txfifo = *src++;
            --tx_remain;
        }
        if (rx_remain && !pio_sm_is_rx_fifo_empty(spi->pio, spi->sm)) {
            *dst++ = *rxfifo;
            --rx_remain;
        }
    }
    while (!pio_sm_is_tx_fifo_empty(spi->pio, spi->sm));
#endif // !SPI_PIO_USE_DMA
}
