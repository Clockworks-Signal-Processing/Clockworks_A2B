/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
* @file: a2bpnp.h
* @brief: This file contains the declaration of APIs provided by PnP Software for the Application.
*
* Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/

#ifndef A2BPNP_H__
#define A2BPNP_H__


/*! \addtogroup Application_Reference API 
 *  @{
 */
#include "pnp/a2bpnp_conf.h"
/*============= D A T A T Y P E S=============*/

/* Application callback enabled with node discovery */
#define A2BAPP_NODE_LEVEL_DISC_CALLBACK
/* Main Node ID */
#define A2B_PNP_MAIN_NODE				   -1
/* Maximum number of the sinks in network */
#define A2B_PNP_CFG_MAX_NUM_SINK_NODES     (A2B_CONF_MAX_NUM_SLAVE_NODES-1u)

/* Length of the Vendor ID */
#define A2B_PNP_VENDOR_ID_LEN		8u

/* Size of RXMASKn registers */
#define A2B_PNP_RXMASKn_SIZE      	8u
/* EEPROM Start address for PnP Content */
#define A2B_PNP_EEPROM_ADRESS_START                     (uint16_t)(0x50)
/* EEPROM Last address for PnP Content */
#define A2B_PNP_EEPROM_ADRESS_END                       (uint16_t)(0x54)
/* EEPROM Page Size in Bytes */
#define A2B_PNP_EEPROM_PAGE_SIZE			64u

/* #define A2B_APP_STATIC_MEMORY_FOR_STACK */


/** SLOT SIZE */
typedef enum A2B_PNP_SLOT_SIZE
{
	A2B_PNP_SLOT_8BITS,
	A2B_PNP_SLOT_12BITS,
	A2B_PNP_SLOT_16BITS,
	A2B_PNP_SLOT_20BITS,
	A2B_PNP_SLOT_24BITS,
	A2B_PNP_SLOT_28BITS,
	A2B_PNP_SLOT_32BITS,
	A2B_PNP_SLOT_NOPREF
}A2B_PNP_SLOT_SIZE;

/** A2B Device Type Information */
typedef enum a2bpnp_DeviceType {
	PDM_MIC = 0u,
	ANALOG_MIC = 1u,
	AUDIO_OUT = 2u,
	AUDIO_IN = 3u,
	MIDI = 4u,
	SENSOR = 5u
}a2bpnp_DeviceType;

/** Information about the Peripheral available on the A2B Device */
typedef struct {
	ADI_A2B_DEVICE_INTERFACE InterfaceType;          /*!< I2C or SPI Peripheral connection to A2B Device */
	a2b_UInt8 PeriphStreamName[A2B_PNP_CFG_PERIPHERAL_NAME_SIZE];                    /*!< Vendor defined Peripheral/Stream name */
	a2b_UInt8 MaxPowerConsump;                         /*!< Maximum Power consumption in watts */
	a2b_UInt16 MaxCurrentDrive;                        /*!< Maximum driven current in milli-Amps */
	a2b_UInt8 Address;                                 /*!< I2C/SPI Address */
	a2b_UInt8 nSourceChannels;                         /*!< Total number of channels sourced by the peripheral */
	a2b_UInt8 nSinkChannels;                           /*!< Total number of channels sinked by the peripheral */
	a2b_UInt8 nRxPins;                                 /*!< Total number of RX pins used by the peripheral */
	a2b_UInt8 RxPinMap;                                /*!< RX Pin numbers used by the peripheral */
	a2b_UInt8 nTxPins;                                 /*!< Total number of TX pins used by the peripheral */
	a2b_UInt8 TxPinMap;                                /*!< TX Pin numbers used by the peripheral */
	a2b_UInt8 nAddrWidth;                              /*!< Address Width */
	a2b_UInt8 nDataWidth;                              /*!< Data Width */
	a2b_UInt8 nCommandWidth;                           /*!< Command Width */
	a2b_UInt16 nPeriConfigUnits;                       /*!< Number of Configuration units */
	a2b_UInt8 nTxGroups;								 /*!< Number of Tx Groups*/
	a2b_UInt8 nRxGroups;								 /*!< Number of Rx Groups*/
	a2b_UInt8 TxGroupID[A2B_PNP_CFG_MAX_NUM_GROUPS];	 /*!< Tx Group ID */
	a2b_UInt8 RxGroupID[A2B_PNP_CFG_MAX_NUM_GROUPS];	 /*!< Rx Group ID */
	a2b_UInt32 pConifUnitAddr;                         /*!< Starting EEPROM Address of configuration units */
}a2bpnp_PeripheralInfo;

/** SLOT SIZE */
typedef enum _A2B_PNP_NODE_REJECTION
{
	A2B_PNP_EXCEED_MAX_NODE_COUNT = 1,
	A2B_PNP_INVALID_MODULE_HEADER,
	A2B_PNP_INVALID_VENDOR_ID,
	A2B_PNP_EXCEED_PWR_BUDGET,
	A2B_PNP_EXCEED_BW_BUDGET,
	A2B_PNP_VENDOR_SPECIFIC
}A2B_PNP_NODE_REJECTION;

typedef struct{
	a2b_UInt8 NodeId;			/*!< Node Position ID */
	A2B_PNP_NODE_REJECTION	rejection;  /*< rejection criteria*/
}a2bpnp_NodeRejectionInfo;

/** A2B NW Stream Information */
typedef struct {
	a2b_UInt8 streamIdx;                                    /*!< Stream Index */
	a2b_UInt8 StreamName[A2B_PNP_CFG_STREAM_NAME_SIZE];                               /*!< Stream name */
	a2b_Int8 SourceNodeIdx;                                 /*!< Node ID of the source node */
	a2b_Int8 DestNodeIdx[A2B_PNP_CFG_MAX_NUM_SINK_NODES];   /*!< Node IDs of the Dest nodes */
	a2b_UInt8 nDestNodes;                                   /*!< Number of Destination nodes */
	a2b_UInt8 nNumChannels;                                 /*!< Number of channels */
	a2b_UInt8 DataWidth;                                    /*!< Data Width */
	a2b_UInt8 SamplingRate;                                 /*!< Sampling rate */
	a2b_UInt8 srcRxGroup;									  /*!< source Rx group */
	a2b_UInt8 destTxGroup[A2B_PNP_CFG_MAX_NUM_SINK_NODES];  /*!< Destination Tx groups */
}a2bpnp_StreamInfo;

/** A2B NW Data Tunnel Stream Information */
typedef struct {
	a2b_Int8 dtNodes[A2B_CONF_MAX_NUM_SLAVE_NODES + 1];			/*!< Data Tunnel active nodes */
	a2b_UInt8 nNodes;				/*!< Number of DT active nodes */
	a2b_UInt8 nDtDnSlots;			/*!< Number of DT slots downstream */
	a2b_UInt8 nDtUpSlots;			/*!< Number of DT slots upstream */
	a2b_Int8 dtOwner;				/*!< DT owner node */
}a2bpnp_DTStreamInfo;

typedef struct{
	a2b_UInt8 GroupID;			/*!< Group ID */
	a2b_UInt8 GroupName[A2B_PNP_CFG_GROUP_NAME_SIZE];		/*!< Group Name */
	a2b_UInt8 nGroupChannels;		/*!< Number of channels in Group */
	a2b_UInt8 GroupChannels[A2B_PNP_CFG_MAX_NUM_GROUP_CHANNELS];	/*!< Channels in Group */
}a2bpnp_GroupInfo;

typedef struct {
	a2b_UInt16 FunctionLength;         /*!< Function Information Length */
	a2b_UInt8 FunctionInfo[A2B_PNP_CFG_FUNCTION_INFO_SIZE];			/*!< Function Information */
	a2b_UInt8 FunctionType;				/*!< Type of Function - MIDI, Sensor, etc.*/
}a2bpnp_DeviceFunction;

typedef struct {
	a2b_UInt16 minPwr;					/*!<minimum power */
	a2b_UInt16 maxPwr;					/*!<Maximum power */	
	a2b_UInt16 maxCurrent;				/*!<Maximum current */
	a2b_Bool   bLocalPowered;			/*!< Buspowered */
}a2bpnp_DevicePwrInfo;

/** A2B Device Information */
typedef struct {
	a2b_UInt8 NodeType;             /*!< Sink / Source / SINK_SOURCE*/
	a2b_UInt8 DeviceName[A2B_PNP_CFG_DEVICE_NAME_SIZE];        /*!< Vendor defined Device name */

	a2b_UInt8 nDeviceType;			/*!< Number of device types supported */
	a2bpnp_DeviceType DeviceType[A2B_PNP_CFG_MAX_NUM_DTYPES]; /*!< List of device types supported */
	a2b_UInt8 vendorID[A2B_PNP_VENDOR_ID_LEN]; 	/*!< Vendor ID */
	a2b_UInt16 nDeviceFunc;			/*!< Number of Device Functions supported */
	a2bpnp_DeviceFunction DevFunc[A2B_PNP_CFG_MAX_NUM_DEVICE_FUNC];	
	a2b_UInt8 nTotalSourceChannels;   /*!< Total number of channels that can be source by the device */
	a2b_UInt8 nTotalSinkChannels;     /*!< Total number of channels that can be sink by the device */
	a2bpnp_PeripheralInfo PeripInfo[A2B_PNP_CFG_MAX_NUM_PERIPH_ON_NODE];   /*!< Information about the peripherals on the device */
	a2b_UInt8 nTxGroups;				/*!< Number of Tx Groups*/
	a2b_UInt8 nRxGroups;				/*!< Number of Rx Groups*/
	a2bpnp_GroupInfo TxGroups[A2B_PNP_CFG_MAX_NUM_GROUPS];	/*!< Tx Group details*/
	a2bpnp_GroupInfo RxGroups[A2B_PNP_CFG_MAX_NUM_GROUPS];	/*!< Rx Group details*/
	a2b_UInt32 txFrameDN[32];			/*!< Tx Frame Buffer downstream*/
	a2b_UInt32 txFrameUP[32];			/*!< Tx Frame Buffer upstream*/
	a2b_UInt32 txxbar[32];			/*!< Tx Crossbar buffer*/
	a2b_UInt8 rxmask[8];				/*!< Rx Mask Buffers */
	a2b_Char swVersion[10];			/*!< Software Version*/
	A2B_PNP_SLOT_SIZE dnSlotSize;	/*!< Down slot size configured*/
	A2B_PNP_SLOT_SIZE upSlotSize;	/*!< Up slot size configured*/
	a2b_UInt8 nDtDnSlots;				/*!< Number of DT slots downstream*/
	a2b_UInt8 nDtUpSlots;				/*!< Number of DT slots upstream */
	a2b_Bool dtEnabled;					/*!< DT enabled/disabled */
	a2b_Bool dtRole;					/*!< DT role, 1- owner, 0- responder */
	a2b_UInt8 eepromAddr;				/*!< EEPROM address */
	a2b_UInt8 vendorData[256];			/*!< Vendor specific data */
	a2b_Int32 vendorDataLen;			/*!< Vendor specific data length */
	a2b_Bool bLocalMute;				/*!<Flag to locally mute the node*/
	a2bpnp_DevicePwrInfo pwrInfo;		/*!< Power related info */
	/* Add Peripheral Count */
	/* Stream index */
}a2bpnp_DeviceInfo;



/*!< Main Node App Information */
typedef struct {
	a2b_UInt8 DeviceName[A2B_PNP_CFG_DEVICE_NAME_SIZE];         /*!< Vendor defined Device name */
	a2b_UInt8 nDeviceType;			/*!< Number of device types supported */
	a2bpnp_DeviceType DeviceType[A2B_PNP_CFG_MAX_NUM_DTYPES];	/*!< List of device types supported */
	a2b_UInt8 nTxGroups;				/*!< Number of Tx Groups*/
	a2b_UInt8 nRxGroups;				/*!< Number of Rx Groups*/
	a2bpnp_GroupInfo TxGroups[A2B_PNP_CFG_MAX_NUM_GROUPS];		/*!< Tx Group details*/
	a2bpnp_GroupInfo RxGroups[A2B_PNP_CFG_MAX_NUM_GROUPS];		/*!< Rx Group details*/
	a2b_Bool dtEnabled;					/*!< DT enabled/disabled */
	a2b_Bool bLocalMute;				/*Flag to locally mute the node*/
}a2bpnp_MainNodeAppInfo;

/*!< A2B Network Information */
typedef struct {
	a2bpnp_DeviceInfo DevInfo[A2B_CONF_MAX_NUM_SLAVE_NODES];           /*!< Information about the connected device and their peripherals in the Nwtwork */
	a2b_UInt8 nDeviceCount;                                              /*!< Number of devices in the Network */
	a2bpnp_DeviceInfo MainNodeInfo;           							/*!< Information about the connected device and their peripherals in the Nwtwork */

	a2bpnp_StreamInfo NWStream[A2B_PNP_CFG_MAX_NUM_NW_STREAMS];        /*!< Stream Information for complete Netwrok */
	a2b_UInt8 nStreamCount;                                              /*!< Total number of the stream defined */
	a2bpnp_DTStreamInfo DTStream[A2B_PNP_CFG_MAX_NUM_NW_DT_STREAMS];   /*!< DT stream information */
	a2b_UInt8 nDTStreamCount;                                            /*!< Total number of the data tunnel streams defined */
}a2bpnp_NetworkInfo;


/*!< Application defined parameters on Device connection */
typedef struct {
	a2b_UInt8 CableLen;                                          /*!< Cable Length */
	/* Others yet to be defined */
}a2bpnp_AppParams;

/*!< Diagnostic Results */
typedef struct {
	a2b_UInt8 bitErrCntMain;                                          /*!< CBit Error count */

}a2b_pnp_diagRes;


/*!< Error Event Type notification to application, these are intergation specific or porting specific */
typedef enum A2B_PNP_ERROR_EVENT_TYPE {
	A2B_PNP_NULL_HANDLE_ERR = 1u,
	A2B_PNP_BCF_FILE_OPEN_ERR = 2u,
	A2B_PNP_MODULE_CATCH_ALL_ERR = 3u,
	A2B_PNP_ROUTING_APPLY_DURING_BD = 4u,
	A2B_PNP_ERROR_05 = 5u,
	A2B_PNP_ERROR_06 = 6u,
	A2B_PNP_ERROR_07 = 7u,
	A2B_PNP_ERROR_08 = 8u
}A2B_PNP_ERROR_EVENT_TYPE;

/*!< Interrupt Event Type notification to application */
typedef enum A2B_PNP_INTERUPPT_EVENT_TYPE {
	A2B_PNP_INTTYPE_IO0PND = 16u,
	A2B_PNP_INTTYPE_IO1PND = 17u,
	A2B_PNP_INTTYPE_IO2PND = 18u,
	A2B_PNP_INTTYPE_IO3PND = 19u,
	A2B_PNP_INTTYPE_IO4PND = 20u,
	A2B_PNP_INTTYPE_IO5PND = 21u,
	A2B_PNP_INTTYPE_IO6PND = 22u,
	A2B_PNP_INTTYPE_IO7PND = 23u
}A2B_PNP_INTERUPPT_EVENT_TYPE;


/*!< Application defined GPIO  over distance paramers  */
typedef struct {
	a2b_UInt8 gpioNum;                                          /*!< GPIO number */
	int8_t nodeId;                                   		  /*!< Node ID */
	a2b_UInt8 invertEnable;                                     /*!< Invert Enable */
	/* Others yet to be defined */
}a2bpnp_GpiodParam;


/*!< CONTROL.XCVRBINV (B-side LVDS polarity) settings for a2bpnp_AppInitParams */
#define A2B_PNP_XCVRBINV_FROM_CFG	(0u)	/*!< Keep the bus configuration value; sub-nodes take the main node's configured value */
#define A2B_PNP_XCVRBINV_OFF		(1u)	/*!< Clear XCVRBINV */
#define A2B_PNP_XCVRBINV_ON			(2u)	/*!< Set XCVRBINV */

/*!< Application defined parameters on Device connection */
typedef struct {
	a2b_UInt8 bDebug;                                          /*!< Enable Debug logs */
	a2b_UInt8 numNodesToDisc;                                  /*!< Number of nodes to discover statically from BCF */
	a2b_UInt8 bTwoStepDisc;									   /*!< Determines whether two step is required or not */
	a2b_UInt8 bGlobalMuteDuringCrossbar;					   /*!< Determines whether audio is globally muted duing the crossbar register updatres */
	a2b_UInt8 bStreamByConnection;							   /*!< Option to create the streams within PnP based on connection info */	
	a2b_UInt8 ignoreE2Prom;									   /*!< Test option to ignore the PnP- EEPROM */
	a2b_UInt8 maxNumSubnodes;								   /*!< Maximum number of subnodes allowed */
	a2b_UInt32 maxPWconsumption;							   /*!< Maximum allowed power consumption in mW */	
	a2b_UInt32  maxCurrentConsumption;        				   /*!< Maximum allowed current consumption in mA */	
	a2b_UInt8  BWWarningLevel;								   /*!< Bandwidth overshoot warning level*/
	a2b_UInt8  BWErrorLevel;								   /*!< Bandwidth overshoot error level*/
	a2b_UInt8  mainXcvrBInv;								   /*!< Main node CONTROL.XCVRBINV, A2B_PNP_XCVRBINV_x */
	a2b_UInt8  subXcvrBInv;									   /*!< Sub-node CONTROL.XCVRBINV (all sub-nodes), A2B_PNP_XCVRBINV_x */
	/* Others yet to be defined */
}a2bpnp_AppInitParams;



/** PnP Event Type notification to application */
typedef enum A2B_PNP_APP_EVENT_TYPE {
	A2B_PNP_NEW_NODE_CONNECTED = 1,        /*!< New node Connected - This event will be raised multiple times if chain of A2B nodes is connected */
	A2B_PNP_NODE_DISCONNECTED = 2,         /*!< Node Disconnected - This event will be raised multiple times if middle node is disconnected */
	A2B_PNP_NODE_REJECTED = 3,             /*!< Node Rejected - This can be due to multiple reason: Signature Authentication failure, Inadequate Power/BW requirements, longer cable lengths */
	A2B_PNP_NW_REDISCOVERY_INITIATED = 4,  /*!< Any critical line faults, main node fault */
	A2B_PNP_BD_COMPLETE_WITH_NEW_NODE = 5, /*!< Audio routing request from Plug and Play Software : Not called if Automatic Audio routing is used */
	A2B_PNP_APPLY_AUDIO_ROUTING = 6,       /*!< Periodic Intimation from Plug and Play Software to apply the Audio routing */
	A2B_PNP_HW_FAULT = 7,					/*!< Hardware Fault */
	A2B_PNP_BASIC_AUTH_FAIL = 8,			/*!< Basic Authorization Failure */
	A2B_PNP_AUDIO_MUTE = 9,					/*!< Audio mute for crossbar register update */
	A2B_PNP_AUDIO_UNMUTE_ROUTING_DONE = 10,	/*!< Completion of register update and audio unmute, this event will appear irrespective of MUTE event */
	A2B_PNP_MAIN_PLL_LOCK_FAILURE = 11,		/*!< Failed to Lock the Main node PLL */
	A2B_PNP_I2C_FAILURE = 12,				/*!< General I2C failure */
	A2B_PNP_NO_VALID_RESP_CYCS = 13,		/*!< No valid response cycles, bandwidth overshoot */
	A2B_PNP_BW_WARNING_LEVEL = 14,			/*!< Bandwidth warning level */
	A2B_PNP_BW_ERROR_LEVEL = 15,			/*!< Bandwidth overshoot */
	A2B_PNP_CONN_IMPORT = 16				/*!< Export Connection Info */
}A2B_PNP_APP_EVENT_TYPE;

/** Location of main node configuration */
typedef enum A2B_PNP_MAINNODE_CONFIG
{
	ADI_A2B_MAINNODE_CONFIG_BCF,       /*!< main node configuration is in BCF file */
	ADI_A2B_MAINNODE_CONFIG_EEPROM,    /*!< main node configuration is in EEPROM on main node */
	ADI_A2B_MAINNODE_CONFIG_FILE       /*!< main node configuration is provided through FILE IO */
}A2B_PNP_MAINNODE_CONFIG;

/** APIs Return Values */
typedef enum A2B_PNP_RESULT
{
	A2B_PNP_RESULT_SUCCESS,             /*!< SUCCESS */
	A2B_PNP_RESULT_FAILED,              /*!< FAILED */
	A2B_PNP_API_NOT_SUPPORTED          /*!< NOT SUPPORTED */
}A2B_PNP_RESULT;

/** PnP State */
typedef enum A2B_PNP_STATE
{
	A2B_PNP_INIT,            	/*!< Init State */
	A2B_PNP_NETWORK_MNG,      	/*!< Network manage state */
	A2B_PNP_NETWORK_HW_FAULT,   /*!< Network Fault state */
	A2B_PNP_BASIC_AUTH_FAILURE,  /*!< Node authentication failure */
	A2B_PNP_MAIN_PLL_NOT_LOCKED,  /*!< Main node PLL not locked */
	A2B_PNP_BLIND_DISC_IN_PROGRESS, /*!< Blind discovery is in progress */
	A2B_PNP_INTERNAL_ERR           /*!< Module internal error state, catch all bad state */
}A2B_PNP_STATE;

/** SPI MODES */
typedef enum A2B_PNP_SPI_MODE
{
	A2B_PNP_SPI_MODE_ATOMIC,           /*!< ATOMIC */
	A2B_PNP_SPI_MODE_BULK_WRITE,       /*!< BULK */
	A2B_PNP_SPI_MODE_FULL_DUPLEX       /*!< FULL DUPLEX */
}A2B_PNP_SPI_MODE;

/** Application defined main node properties */
typedef struct MainNodeProp
{
	A2B_PNP_MAINNODE_CONFIG mainNodeCfg; /*!< Location of main node prop- busconfig/EEPROM/File */
	a2b_UInt32 eepromAddr;               /*!< EEPROM Addr if main node configuration is available on EEPROM */
	a2b_UInt32 pollTime;                 /*!< Polling interval in ms */
	a2b_UInt32 periodicDiscInterval;     /*!< Interval to attempt discovery for possible new nodes */
	a2b_UInt8* pFilePath;                /*!< File path if main node configuration is stored in file */
	ADI_A2B_BCD* pBcf;                   /*! BCF file*/

	/* TODO: Add more fields */
}a2bpnp_MainNodeProp;

/** Hw Fault results */
typedef struct {
	const a2b_Char *faultStatus;							/*! String indicating line fault */
	a2b_Int16 faultNode;		    						/*! Node number at which fault being reported */
	a2b_UInt8 faultCode;									/*! Fault code */
	A2B_PNP_STATE stateDuringFault;							/*! This field indicate when the fault is reported */	
}a2b_pnp_faultRes;

/** Mute Status Info */
typedef struct {
	a2b_Bool globalNetworkMute;										/*!< Indication of Impednding network mute */
	a2b_Bool mainNodeMute;											/*!< Indication of Impending  mute for main node*/											
	a2b_Bool subNodeMute[A2B_CONF_MAX_NUM_SLAVE_NODES];				/*!< Indication of Impending  mute for subnodes */
}a2b_pnp_muteStatus;

/** PnP HANDLE for application */
typedef void* A2B_PNP_HANDLE;

/* PnP Application callback declaration */
typedef void (A2B_CALL * a2b_PnPCallbackFunc)(A2B_PNP_APP_EVENT_TYPE event, void* data, void* CallbackParam);

/** PnP Application callback declaration */
typedef a2b_UInt8 (* PNP_CALLBACK) (   
		/** Client supplied callback param */
		A2B_PNP_HANDLE hPnp,   
		/** Event ID specific to the Driver/Service */
        a2b_UInt32   Event,    
		/** Pointer to the event specific argument */
        void      *pArg);            

/** PnP Interrupt callback declaration */
typedef a2b_UInt8 (* INTR_CALLBACK) (   
		/** Client supplied callback param */
		A2B_PNP_HANDLE hPnp,   
		/** Event ID specific to the Driver/Service */
        uint32_t   Event,  
		/** Pointer to the event specific argument */
        void      *pArg);            

/** PnP Error callback declaration */
typedef a2b_UInt8 (* ERR_CALLBACK) (
        /** Client supplied callback param */
		A2B_PNP_HANDLE hPnp,
        /** Event ID specific to the Driver/Service */         
        uint32_t   Event,
        /** Pointer to the event specific argument */
        void      *pArg);

typedef struct _connElemntBychid
{
	a2b_Char chipid[6];
	a2b_UInt8 grpNo;
}connElemntbyChipId;

typedef struct _connPairBychipID
{
	connElemntbyChipId dst;
	connElemntbyChipId src;
}connPairByChipId;

/*********************************** A2B PnP APIs available to the Application ***********************************/


/* Initialize the PnP Feature. Application registers a callback function with PnP module which is called by PnP Module to report any events to application */
A2B_PNP_HANDLE a2b_pnp_Init(a2b_UInt8 nChainIndex, a2bpnp_MainNodeProp* pCfg, PNP_CALLBACK pfCallback, INTR_CALLBACK pfIntrCallback, ERR_CALLBACK pfErrCallback);

/* De-Initialize the PnP Feature */
A2B_PNP_RESULT a2b_pnp_DeInit(A2B_PNP_HANDLE hPnp);

/* This API can be called to set initial parameters */
A2B_PNP_RESULT a2b_pnp_SetInitParams(A2B_PNP_HANDLE hPnp, a2bpnp_AppInitParams *PnpInitParams);

/* This API can be called to configure main node parameters */
A2B_PNP_RESULT a2b_pnp_ConfigureMainNode(A2B_PNP_HANDLE hPnp, a2bpnp_MainNodeAppInfo *MainNodeAppInfo);

/* Start the Network Discovery */
A2B_PNP_RESULT a2b_pnp_StartDiscovery(A2B_PNP_HANDLE hPnp);

/* Pause the attempt for new node Discovery */
A2B_PNP_RESULT a2b_pnp_PauseNewNodeDisc(A2B_PNP_HANDLE hPnp);

/* Resume the attempt for new node Discovery */
A2B_PNP_RESULT a2b_pnp_ResumeNewNodeDisc(A2B_PNP_HANDLE hPnp);

A2B_PNP_RESULT a2b_pnp_ClearHwFaults(A2B_PNP_HANDLE hPnp, a2b_UInt8 bReStartFromMain);

/* PnP Task: Application shall call this tasks periodically to continue the periodic discovery attempt, detection of nodes disconnected, Stack Tick */
A2B_PNP_RESULT a2b_pnp_Task(A2B_PNP_HANDLE hPnp);


/* Retrieves the complete A2B NW information */
A2B_PNP_RESULT a2b_pnp_GetNWInfo(A2B_PNP_HANDLE hPnp, a2bpnp_NetworkInfo **NetworkInfo);
/* This API can be called to get timing information */
A2B_PNP_RESULT a2b_pnp_GetDiscTimeInfo(A2B_PNP_HANDLE hPnp, a2b_Int32 nodId, a2b_UInt32 *discTimeMs);

A2B_PNP_RESULT a2b_pnp_GetDevicePwrInfo(A2B_PNP_HANDLE hPnp, a2b_Int32 nodeId, a2bpnp_DevicePwrInfo *pPwrInfo);

A2B_PNP_RESULT a2b_pnp_GetVersion( a2b_UInt32* major, a2b_UInt32* minor, a2b_UInt32* patch);

A2B_PNP_RESULT a2b_pnp_GetNwBW(A2B_PNP_HANDLE hPnp, a2b_Int32* pNode, a2b_UInt32* pUsage);

A2B_PNP_RESULT a2b_pnp_GetModuleState(A2B_PNP_HANDLE hPnp, A2B_PNP_STATE* peState);

A2B_PNP_RESULT a2b_pnp_GetVendorID(A2B_PNP_HANDLE hPnp, a2b_Int32 nodeId, a2b_UInt8 *vendorID);

/* This API can be called to raed supplier information */
A2B_PNP_RESULT a2b_pnp_ReadOEMInfo(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeId, a2b_UInt8* buf);

/* Sets the Audio routing for A2B Network */
A2B_PNP_RESULT a2b_pnp_SetAudioRt(A2B_PNP_HANDLE hPnp, a2bpnp_StreamInfo *NWStream, a2b_UInt8 nStreamCount, a2b_UInt8 bIsAppend);

/* This API can be called to get default DT stream parameters */
A2B_PNP_RESULT a2b_pnp_GetDefaultDTStream(A2B_PNP_HANDLE hPnp, a2bpnp_DTStreamInfo *DTStream, a2b_UInt8* dtStreamCount);

/*Sets the SPI DT routing for A2B Network */
A2B_PNP_RESULT a2b_pnp_SetSPIDTRt(A2B_PNP_HANDLE hPnp, a2bpnp_DTStreamInfo *DTStream, a2b_UInt8 nDTCount, a2b_UInt8 bIsAppend);

/* This API can be used to clear existing data tunnel */
A2B_PNP_RESULT a2b_pnp_ClearTunnel(A2B_PNP_HANDLE hPnp, a2bpnp_DTStreamInfo *DTStream);

A2B_PNP_RESULT a2b_pnp_AddConByNodeId(A2B_PNP_HANDLE hPnp, a2b_Int32 srcNodeId, a2b_Int32 srcNodeGroup, a2b_Int32 dstNodeId, a2b_Int32 dstNodeGroup);

A2B_PNP_RESULT a2b_pnp_ClearAllCon(A2B_PNP_HANDLE hPnp);

A2B_PNP_RESULT a2b_pnp_GetConnectionList(A2B_PNP_HANDLE hPnp, connPairByChipId *NWCon, a2b_UInt8 *streamCnt);

A2B_PNP_RESULT a2b_pnp_ImportConnections(A2B_PNP_HANDLE hPnp, connPairByChipId *NWStream, int numCon);

A2B_PNP_RESULT a2b_pnp_RemoveConByNodeId(A2B_PNP_HANDLE hPnp, a2b_Int32 srcNodeId, a2b_Int32 srcNodeGroup, a2b_Int32 dstNodeId, a2b_Int32 dstNodeGroup);

/* Apply both Audio and SPI DT routing for A2B Network */
A2B_PNP_RESULT a2b_pnp_ApplyRouting(A2B_PNP_HANDLE hPnp);

A2B_PNP_RESULT a2b_pnp_MuteNetwork(A2B_PNP_HANDLE hPnp, a2b_Bool bMute);

/* Write A2B register on main/sub nodes */
A2B_PNP_RESULT a2b_pnp_WriteReg(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt8 RegAddr, a2b_UInt8 Value);

/* Read A2B register on main/sub nodes */
A2B_PNP_RESULT a2b_pnp_ReadReg(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt8 RegAddr, a2b_UInt8* Value);

/* Read memory/register of the I2C Peripheral */
A2B_PNP_RESULT a2b_pnp_I2CReadPeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite, void* wBuf, a2b_UInt16 nRead, void* rBuf);


/* Write memory/register of the I2C Peripheral */
A2B_PNP_RESULT a2b_pnp_I2CWritePeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite, void* wBuf);


/* Read memory/register of the I2C Peripheral */
A2B_PNP_RESULT a2b_pnp_SPIReadPeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite, void* wBuf, a2b_UInt16 nRead, void* rBuf);


/* Write memory/register of the I2C Peripheral */
A2B_PNP_RESULT a2b_pnp_SPIWritePeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite, void* wBuf);

/* Write memory/register of the SPi Peripheral */
A2B_PNP_RESULT a2b_pnp_SPIDTWriteReadPeri(A2B_PNP_HANDLE hPnp, a2b_SpiWrRdParams * pSpiWrRdParams/*a2b_Int16 nodeAddr, a2b_UInt16 SpiSSel, a2b_UInt16 nWrite, void* wBuf*/);

/* Write memory/register of the SPi Peripheral */
A2B_PNP_RESULT a2b_pnp_SPIPeriSetMode(A2B_PNP_HANDLE hPnp, a2b_SpiConfig* config);

/* This API can be called to read virtual IO port */
A2B_PNP_RESULT a2b_pnp_ReadVirtualIOPort(A2B_PNP_HANDLE hPnp, a2b_UInt8 virtualportNum, a2b_UInt8* pVal);


/* This API can be called to configure GPIO over Distance */
A2B_PNP_RESULT a2b_pnp_SetupGPIOOverDistance(A2B_PNP_HANDLE hPnp, a2b_UInt8 virtualportNum, a2bpnp_GpiodParam participatorList[], a2b_UInt8 count );

/* Clear GPIO over distance settings */
A2B_PNP_RESULT a2b_pnp_ClearGPIOOverDistance(A2B_PNP_HANDLE hPnp, a2b_UInt8 virtualportNum, a2bpnp_GpiodParam participatorList[], a2b_UInt8 count );


A2B_PNP_RESULT a2b_pnp_UpdatePnPModuleInfoAll(A2B_PNP_HANDLE hPnp, a2b_Char* fileName, a2b_UInt16 eePromAddr);

A2B_PNP_RESULT a2b_pnp_UpdatePnPModuleInfo( A2B_PNP_HANDLE hPnp, a2b_Char* fileName, a2b_Int32 nodeId, a2b_UInt16 eePromAddr);

A2B_PNP_RESULT a2b_pnp_RejectNode(A2B_PNP_HANDLE hPnp, a2b_Int32 nodeId);

/* This API can be called after new device discovery to set certain parameters */
A2B_PNP_RESULT a2b_pnp_SetNodeParams(A2B_PNP_HANDLE hPnp, a2b_UInt8 nodeAddr, a2bpnp_AppParams *PnpParams);

/* This API can be called to get certain parameters for a give device */
A2B_PNP_RESULT a2b_pnp_GetNodeParams(A2B_PNP_HANDLE hPnp, a2b_UInt8 nodeAddr, a2bpnp_AppParams **PnpParams);

A2B_PNP_RESULT a2b_pnp_RunNwDiagnostics(A2B_PNP_HANDLE hPnp, a2b_pnp_diagRes* pRes);

A2B_PNP_RESULT a2b_pnp_AddConByChipId(A2B_PNP_HANDLE hPnp, connPairByChipId pair);

#endif /* A2BPNP_H__ */

/**
 @}
*/
