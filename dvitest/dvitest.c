// Copyright (c) 2025 Konrad Beckmann
// Copyright (c) 2024 Raspberry Pi (Trading) Ltd.

// SPDX-License-Identifier: GPL-3.0

// This can only run on RP2350.

// Generate DVI output using the command expander and TMDS encoder in HSTX.

// This example requires an external digital video connector connected to
// GPIOs 12 through 19 (the HSTX-capable GPIOs) with appropriate
// current-limiting resistors, e.g. 270 ohms. The pinout used in this example
// matches the Pico DVI Sock board, which can be soldered onto a Pico 2:
// https://github.com/Wren6991/Pico-DVI-Sock

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"
#include "pico/sem.h"

#include <string.h>

#include "ndsl_video.pio.h"

// ----------------------------------------------------------------------------
// DVI constants

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define MODE_H_SYNC_POLARITY 0
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 640
#define FB_WIDTH             (MODE_H_ACTIVE_PIXELS / 2)
#define DS_WIDTH             256

#define MODE_V_SYNC_POLARITY 0
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  480
#define FB_HEIGHT            (MODE_V_ACTIVE_LINES / 2)
#define DS_HEIGHT            192

#define MODE_H_TOTAL_PIXELS ( \
    MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + \
    MODE_H_BACK_PORCH  + MODE_H_ACTIVE_PIXELS \
)
#define MODE_V_TOTAL_LINES  ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + \
    MODE_V_BACK_PORCH  + MODE_V_ACTIVE_LINES \
)

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

/*
    32-bit Little endian

    |[BYTE 3]|[BYTE 2]|[BYTE 1]|[BYTE 0]|
    |31   25 |  20  16|  12   8|       0|
    |v     v |   v   v|   v   v|       v|
    |xxxCCC54|3210xxxx|xxxx5432|10543210|
    |   SGDrr|rrrrHHHH|HHHHgggg|ggbbbbbb|
    |   |||
    |   |||_DCLK - Pixel Clock [REQ]
    |   ||___GSP  - Global Start Pulse? Start of frame [REQ]
    |   |____SPL  - Horizontal Start Pulse? Active pixels after 2 CLK 

*/

#define GET_NUM_BITS(_msb, _lsb) ((_msb) - (_lsb) + 1)
#define GET_ROT_LSB(_msb, _lsb)  ((32 - 8 + GET_NUM_BITS((_msb), (_lsb)) + (_lsb)) % 32)

#define COLOR_MSB_R 25
#define COLOR_LSB_R 20
#define COLOR_ROT_R GET_ROT_LSB(COLOR_MSB_R, COLOR_LSB_R)

#define COLOR_MSB_G 11
#define COLOR_LSB_G  6
#define COLOR_ROT_G GET_ROT_LSB(COLOR_MSB_G, COLOR_LSB_G)

#define COLOR_MSB_B 5
#define COLOR_LSB_B 0
#define COLOR_ROT_B GET_ROT_LSB(COLOR_MSB_B, COLOR_LSB_B)

static uint32_t __attribute__((aligned(4))) ds_framebuf[DS_WIDTH * DS_HEIGHT];
static uint32_t __attribute__((aligned(4))) linebuf[FB_WIDTH];
#include "mountains_640x480_rgb332.h"
// #include "rainbow_320x240_rgb666.h"
// #define framebuf out_raw


// ----------------------------------------------------------------------------
// HSTX command lists

// Lists are padded with NOPs to be >= HSTX FIFO size, to avoid DMA rapidly
// pingponging and tripping up the IRQs.

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1,
    HSTX_CMD_NOP
};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1,
    HSTX_CMD_NOP
};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS
};

// ----------------------------------------------------------------------------
// DMA logic

#define DMACH_PING 0
#define DMACH_PONG 1
#define DMACH_NDSL_PING 2
#define DMACH_NDSL_PONG 3

// static uint8_t border_color = 0x07;
static uint8_t border_color = 0;

// First we ping. Then we pong. Then... we ping again.
static bool dma_pong = false;
static bool dma_ndsl_pong = false;

// A ping and a pong are cued up initially, so the first time we enter this
// handler it is to cue up the second ping after the first ping has completed.
// This is the third scanline overall (-> =2 because zero-based).
static uint v_scanline = 2;

// During the vertical active period, we take two IRQs per scanline: one to
// post the command list, and another to post the pixels.
static bool vactive_cmdlist_posted = false;

void __scratch_x("") dma_irq0_handler() {

    if (dma_hw->ints0 & ((1<<DMACH_PING) | (1<<DMACH_PONG)) )
    {

        // dma_pong indicates the channel that just finished, which is the one
        // we're about to reload.
        uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
        dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
        dma_hw->intr = 1u << ch_num;
        dma_pong = !dma_pong;

        if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
            ch->read_addr = (uintptr_t)vblank_line_vsync_on;
            ch->transfer_count = count_of(vblank_line_vsync_on);
        } else if (v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH) {
            ch->read_addr = (uintptr_t)vblank_line_vsync_off;
            ch->transfer_count = count_of(vblank_line_vsync_off);
        } else if (!vactive_cmdlist_posted) {
            ch->read_addr = (uintptr_t)vactive_line;
            ch->transfer_count = count_of(vactive_line);
            vactive_cmdlist_posted = true;
        } else {
            // 2x upscaling, 320x240 -> 640x480
            unsigned line = (v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES)) / 2;
            // ch->read_addr = (uintptr_t)&framebuf[line * (FB_WIDTH * 4)]; // when using uint8_t framebuffer
            // ch->read_addr = (uintptr_t)&framebuf[line * FB_WIDTH];          // when using uint32_t framebuffer

            // Copy active pixels into the line buffer
            if (line >= 24 && (line - 24 < DS_HEIGHT))
            {
                memcpy(&linebuf[32], &ds_framebuf[(line - 24) * DS_WIDTH], DS_WIDTH * 4);
            }
            else
            {
                memset(linebuf, border_color, FB_WIDTH * 4); // white borders
            }

            ch->read_addr = (uintptr_t)&linebuf[0];
            
            ch->transfer_count = MODE_H_ACTIVE_PIXELS / 2;
            vactive_cmdlist_posted = false;
        }

        if (!vactive_cmdlist_posted) {
            v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
        }
    }
    else if (dma_hw->ints0 & ((1<<DMACH_NDSL_PING) | (1<<DMACH_NDSL_PONG) ))
    {
        dma_hw->ints0 = ((1<<DMACH_NDSL_PING) | (1<<DMACH_NDSL_PONG) );
        // // dma_pong indicates the channel that just finished, which is the one
        // // we're about to reload.
        // uint ch_num = dma_ndsl_pong ? DMACH_NDSL_PONG : DMACH_NDSL_PING;
        // // uint ch_num = dma_ndsl_pong ? DMACH_NDSL_PING : DMACH_NDSL_PONG;
        // dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
        // dma_hw->intr = 1u << ch_num;
        // dma_ndsl_pong = !dma_ndsl_pong;

        // border_color++;

        // ch->write_addr = (io_rw_32) ds_framebuf;
        // ch->read_addr = (io_rw_32) mountains_640x480;
        // ch->transfer_count = DS_WIDTH * DS_HEIGHT;
    }
}


// void __scratch_x("") dma_irq1_handler() {
//     // dma_pong indicates the channel that just finished, which is the one
//     // we're about to reload.
//     uint ch_num = dma_ndsl_pong ? DMACH_NDSL_PONG : DMACH_NDSL_PING;
//     dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
//     dma_hw->intr = 1u << ch_num;
//     dma_ndsl_pong = !dma_ndsl_pong;

//     border_color++;

//     ch->read_addr = (io_rw_32) mountains_640x480;
//     ch->transfer_count = DS_WIDTH * DS_HEIGHT;
// }

// ----------------------------------------------------------------------------
// Main program

static __force_inline uint32_t make_rgb666(uint32_t r, uint32_t g, uint32_t b) {
    return (
        (((r >> 2) & 0x3f) << 20) | 
        (((g >> 2) & 0x3f) << 6)  | 
        (((b >> 2) & 0x3f) << 0)
    );
}

int main(void)
{
    memset(ds_framebuf, 0xff, sizeof(ds_framebuf));

    for (int y = 0; y < DS_HEIGHT; y++)
    {
        for (int x = 0; x < DS_WIDTH; x++)
        {
            unsigned xx;
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;

            if (x < 64)
            {
                r = x << 2;
            }
            else if (x < 64 * 2)
            {
                xx = x - 64;
                g = xx << 2;

            }
            else if (x < 64 * 3)
            {
                xx = x - 64 * 2;
                b = xx << 2;
            }
            else if (x < 64 * 4)
            {
                xx = x - 64 * 3;
                r = xx << 2;
                g = xx << 2;
            }
            else if (x < 64 * 5)
            {
                xx = x - 64 * 4;
                g = xx << 2;
                b = xx << 2; 
            }

            // r = 0xff;
            // g = 0;
            // b = 0;

            ds_framebuf[y * DS_WIDTH + x] = make_rgb666(r, g, b);
        }
    }

    const PIO pio = pio0;
    const uint sm_video = 0;
    
    // Video
    uint offset = pio_add_program(pio, &ndsl_video_program);
    ndsl_video_program_init(pio, sm_video, offset);
    // pio_sm_set_enabled(pio, sm_video, true);


    // Configure HSTX's TMDS encoder for RGB666
    hstx_ctrl_hw->expand_tmds =
        5  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
        COLOR_ROT_R << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        5  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        COLOR_ROT_G << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        5  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        COLOR_ROT_B << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

    // Pixels (TMDS) come in 4 8-bit chunks. Control symbols (RAW) are an
    // entire 32-bit word.
    hstx_ctrl_hw->expand_shift =
        2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    // Serial output config: clock period of 5 cycles, pop from command
    // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // Note we are leaving the HSTX clock at the SDK default of 125 MHz; since
    // we shift out two bits per HSTX clock cycle, this gives us an output of
    // 250 Mbps, which is very close to the bit clock for 480p 60Hz (252 MHz).
    // If we want the exact rate then we'll have to reconfigure PLLs.

    // HSTX outputs 0 through 7 appear on GPIO 12 through 19.
    // Pinout on Pico DVI sock:
    //
    //   GP12 D0+  GP13 D0-
    //   GP14 CK+  GP15 CK-
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  GP19 D1-

    // Assign clock pair to two neighbouring pins:
    hstx_ctrl_hw->bit[2] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[3] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
        // For each TMDS lane, assign it to the correct GPIO pair based on the
        // desired pinout:
        static const int lane_to_output_bit[3] = {0, 6, 4};
        int bit = lane_to_output_bit[lane];
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits =
            (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        hstx_ctrl_hw->bit[bit    ] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, 0); // HSTX
    }

    // Both channels are set up identically, to transfer a whole scanline and
    // then chain to the opposite channel. Each time a channel finishes, we
    // reconfigure the one that just finished, meanwhile the opposite channel
    // is already making progress.
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(
        DMACH_PING,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(
        DMACH_PONG,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    // dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG) | (1u << DMACH_NDSL_PING) | (1u << DMACH_NDSL_PONG);
    // dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG) | (1u << DMACH_NDSL_PING) | (1u << DMACH_NDSL_PONG);

    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq0_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // dma_channel_start(DMACH_PING);


    ///////////////////////////////
    // Capture NDSL video using PIO with DMA

    // Setup IRQ for the NDSL video PIO DMA
    c = dma_channel_get_default_config(DMACH_NDSL_PING);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    // channel_config_set_chain_to(&c, DMACH_NDSL_PONG);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_video, false));
    dma_channel_configure(
        DMACH_NDSL_PING,
        &c,
        &ds_framebuf[0],                    // write
        // mountains_640x480,
        (io_rw_32*)&pio->rxf[sm_video], // read
        // dma_encode_transfer_count_with_self_trigger(DS_WIDTH * DS_HEIGHT / 8),           // count
        DS_WIDTH * DS_HEIGHT,           // count
        false
    );


    // c = dma_channel_get_default_config(DMACH_NDSL_PONG);
    // channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    // channel_config_set_read_increment(&c, true);
    // channel_config_set_write_increment(&c, true);
    // // channel_config_set_chain_to(&c, DMACH_NDSL_PING);
    // // channel_config_set_dreq(&c, pio_get_dreq(pio, sm_video, false));
    // dma_channel_configure(
    //     DMACH_NDSL_PONG,
    //     &c,
    //     &ds_framebuf[0],                    // write
    //     mountains_640x480,
    //     // (io_rw_32*)&pio->rxf[sm_video], // read
    //     DS_WIDTH * DS_HEIGHT,           // count
    //     false
    // );

    // dma_hw->ints1 |= (1u << DMACH_NDSL_PING) | (1u << DMACH_NDSL_PONG);
    // dma_hw->inte1 |= (1u << DMACH_NDSL_PING) | (1u << DMACH_NDSL_PONG);
    // dma_hw->ints0 |= (1u << DMACH_NDSL_PING) | (1u << DMACH_NDSL_PONG);
    // dma_hw->inte0 |= (1u << DMACH_NDSL_PING) | (1u << DMACH_NDSL_PONG);

    // irq_set_exclusive_handler(DMA_IRQ_1, dma_irq1_handler);
    // irq_set_enabled(DMA_IRQ_1, true);

    // dma_channel_set_irq0_enabled(DMACH_NDSL_PING, true);
    // dma_channel_set_irq0_enabled(DMACH_NDSL_PONG, true);

    // bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // dma_channel_start(DMACH_PING);
    // dma_channel_start(DMACH_NDSL_PING);
    // dma_channel_start(DMACH_NDSL_PONG);
    dma_start_channel_mask((1 << DMACH_PING) | (1 << DMACH_NDSL_PING));
    // dma_start_channel_mask((1 << DMACH_PING) | (1 << DMACH_NDSL_PONG));

    pio_sm_set_enabled(pio, sm_video, true);



    // uint32_t data;
    // unsigned i = 0;
    // while (1) {
    //     data = pio_sm_get_blocking(pio, sm_video);
    //     ds_framebuf[i] = data;
    //     i++;
    //     i = i % (DS_WIDTH * DS_HEIGHT);
    // };

    uint ch_num = dma_ndsl_pong ? DMACH_NDSL_PONG : DMACH_NDSL_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];

    while (1)
    {
        // temp++;
        // sleep_ms(1000);

        // border_color++;

        dma_channel_wait_for_finish_blocking(DMACH_NDSL_PING);
        pio_sm_set_enabled(pio, sm_video, false);
        pio_sm_clear_fifos(pio, sm_video);

        ch->write_addr = (io_rw_32) ds_framebuf;
        ch->read_addr = (io_rw_32) (io_rw_32*)&pio->rxf[sm_video];
        ch->transfer_count = DS_WIDTH * DS_HEIGHT;
        // dma_channel_configure(
        //     DMACH_NDSL_PING,
        //     &c,
        //     &ds_framebuf[0],                    // write
        //     // mountains_640x480,
        //     (io_rw_32*)&pio->rxf[sm_video], // read
        //     // dma_encode_transfer_count_with_self_trigger(DS_WIDTH * DS_HEIGHT / 8),           // count
        //     DS_WIDTH * DS_HEIGHT,           // count
        //     false
        // );
        dma_channel_start(DMACH_NDSL_PING);

        ndsl_video_program_init(pio, sm_video, offset);

        pio_sm_set_enabled(pio, sm_video, true);

    }


    while (1)
        __wfi();
}
