/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : platform_inits.c

   Description: This file is resposnible for platform specific initialization

   Developed by: Consumer system application team, Bangalore, India

******************************************************************************/
/*============= I N C L U D E S =============*/

#include "a2bapp_rp2040.h"
#include "adi_a2b_externs.h"
#include "assert.h"
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"
#include <stdint.h>
#include "cmd_platform.h"
#include "cmd_queue.h"
#include "pico/malloc.h"
#include <string.h>
#include "pico/stdlib.h"
#include "stdio.h"

/*============= D E F I N E S =============*/

static void prvSetupHardware( void )
{
    // Initialize stdio for board.

    // Configure GPIO and enable CODEC
    gpio_init(MIDI2A2B_PWR_EN_CODEC);
    gpio_set_dir(MIDI2A2B_PWR_EN_CODEC, true);  // output
    gpio_put(MIDI2A2B_PWR_EN_CODEC, false); 

    // Configure MMP and set as no data ready
    gpio_init(MIDI2A2B_MMP);
    gpio_set_dir(MIDI2A2B_MMP, true);   // output
    gpio_put(MIDI2A2B_MMP, false);      // no data to transmit, just booting up

    // Configure GPIOs and disable Phantom Power
    gpio_init(MIDI2A2B_PWR_PHANTOM_EN_CHGPUMP);
    gpio_set_dir(MIDI2A2B_PWR_PHANTOM_EN_CHGPUMP, true); // output
    gpio_put(MIDI2A2B_PWR_PHANTOM_EN_CHGPUMP, false); // disable

    gpio_init(MIDI2A2B_PWR_PHANTOM_EN_L);
    gpio_set_dir(MIDI2A2B_PWR_PHANTOM_EN_L, true); // output
    gpio_put(MIDI2A2B_PWR_PHANTOM_EN_L, false); // disable

    gpio_init(MIDI2A2B_PWR_PHANTOM_EN_R);
    gpio_set_dir(MIDI2A2B_PWR_PHANTOM_EN_R, true); // output
    gpio_put(MIDI2A2B_PWR_PHANTOM_EN_R, false); // disable

    // Configure GPIO and disable USB Host Port
    gpio_init(MIDI2A2B_USBH_PWR_EN);
    gpio_set_dir(MIDI2A2B_USBH_PWR_EN, true); // output
    gpio_put(MIDI2A2B_USBH_PWR_EN, false); // disable

    // Configure GPIOS and disable boost
    gpio_init(MIDI2A2B_12dB_BOOST_EN_L_SE);
    gpio_set_dir(MIDI2A2B_12dB_BOOST_EN_L_SE, true); // output
    gpio_put(MIDI2A2B_12dB_BOOST_EN_L_SE, false); // disable

    gpio_init(MIDI2A2B_12dB_BOOST_EN_L_DIFF);
    gpio_set_dir(MIDI2A2B_12dB_BOOST_EN_L_DIFF, true); // output
    gpio_put(MIDI2A2B_12dB_BOOST_EN_L_DIFF, false); // disable

    gpio_init(MIDI2A2B_12dB_BOOST_EN_R_SE);
    gpio_set_dir(MIDI2A2B_12dB_BOOST_EN_R_SE, true); // output
    gpio_put(MIDI2A2B_12dB_BOOST_EN_R_SE, false); // disable

    gpio_init(MIDI2A2B_12dB_BOOST_EN_R_DIFF);
    gpio_set_dir(MIDI2A2B_12dB_BOOST_EN_R_DIFF, true); // output
    gpio_put(MIDI2A2B_12dB_BOOST_EN_R_DIFF, false); // disable

    // Configure and do not force reset of A2B 2437
    gpio_init(MIDI2A2B_2437_FORCE_RST);
    gpio_set_dir(MIDI2A2B_2437_FORCE_RST, true); // output
    gpio_put(MIDI2A2B_2437_FORCE_RST, false); // disable
}

void platform_init()
{
#if 0
    prvSetupHardware();
    sleep_ms(1000);

    gpio_put(MIDI2A2B_PWR_EN_CODEC, true); 
    sleep_ms(1000);
#endif
}