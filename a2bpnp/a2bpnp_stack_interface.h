/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
* @file: a2bpnp_stack_interface.h
* @brief: This file contains the declaration of APIs provided by PnP library for the Application.
*
* Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/
#ifndef A2BPNP_STACK_INTERFACE_H__
#define A2BPNP_STACK_INTERFACE_H__

a2b_UInt8 a2bpnp_stkinterface_Init(a2b_App_t * pApp_Info);
a2b_UInt8 a2bpnp_stkinterface_getBdd(a2b_App_t * pApp_Info);
a2b_UInt8 a2bpnp_stkinterface_stkAlloc(a2b_App_t * pApp_Info);
a2b_UInt8 a2bpnp_stkinterface_SetAcesInterface(a2b_App_t * pApp_Info);
a2b_UInt8 a2bpnp_stkinterface_SetNotification(a2b_App_t * pApp_Info);
a2b_UInt8 a2bpnp_stkinterface_StartTimerForBlindDiscovery(a2b_App_t * pApp_Info);
a2b_UInt8 a2b_sendPartialDiscoveryMessage(a2b_App_t *pApp_Info);
a2b_UInt8 a2b_sendInitDiscoveryMessage(a2b_App_t *pApp_Info);
a2b_UInt8 a2b_stop(a2b_App_t *pApp_Info);
a2b_UInt8 a2b_setupPwrDiag(a2b_App_t *pApp_Info);
a2b_HResult a2b_AppBulkReadReg(struct a2b_StackContext* ctx, a2b_Int16 nodeAddr, a2b_UInt32 reg, a2b_UInt8 value[], a2b_UInt8 count);

/* addtional dependecy on PAL */
a2b_HResult a2b_pal_FileRead(a2b_Handle hnd, a2b_UInt16 offset, a2b_UInt16 nRead,
        a2b_Byte* rBuf);
a2b_HResult a2b_pal_FileOpen(A2B_ECB* ecb, char* url);
a2b_HResult a2b_pal_FileClose(A2B_ECB* ecb);

#endif /* A2BPNP_STACK_INTERFACE_H__ */
