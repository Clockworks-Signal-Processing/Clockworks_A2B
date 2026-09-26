/*******************************************************************************
Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************/


#ifndef __A2BAPP_SC59X_H__
#define __A2BAPP_SC59X_H__

/*============= I N C L U D E S =============*/

#include <a2b/error.h>
/*============= D E F I N E S =============*/
/* Add your custom header content here */
#ifdef A2B_FEATURE_PNP
/**
 * This option need to enable to let application use Automatic Audio routing
 **/
//#define ADI_CONFIG_ENABLE_PNP_DEFAULT_ROUTING
#endif
#define TERMINAL_APP_VERSION   "0.1.0"


// Board XPNSN Pins
#define MIDI2A2B_MAIN_XPNSN_01   6   // J15.24
#define MIDI2A2B_MAIN_XPNSN_02   7   // J15.26
#define MIDI2A2B_MAIN_XPNSN_03   8   // J15.28

// Peripheral Defines
#define MIDI2A2B_USBH_PWR_EN    29

// A2B Interface Defines
#define MIDI2A2B_IRQ            16
#define MIDI2A2B_MOSI           12
#define MIDI2A2B_MISO           13
#define MIDI2A2B_NCS            14
#define MIDI2A2B_SCLK           15
#define MIDI2A2B_SDA            16
#define MIDI2A2B_SCL            17
#define MIDI2A2B_MMP            18
#define MIDI2A2B_USBi_SENSE     19

// Power Mgmt IO Defines
#define MIDI2A2B_PWR_EN_CODEC           20
#define MIDI2A2B_PWR_PHANTOM_EN_CHGPUMP 28
#define MIDI2A2B_PWR_PHANTOM_EN_L       22
#define MIDI2A2B_PWR_PHANTOM_EN_R       24

// Analog boosts
#define MIDI2A2B_12dB_BOOST_EN_L_SE     21
#define MIDI2A2B_12dB_BOOST_EN_L_DIFF   23
#define MIDI2A2B_12dB_BOOST_EN_R_SE     25
#define MIDI2A2B_12dB_BOOST_EN_R_DIFF   26

// Misc
#define MIDI2A2B_2437_FORCE_RST         27
#endif /* __A2BAPP_WIN32_H__ */
