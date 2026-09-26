/*******************************************************************************
Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bpnp_conn_mng.cpp

   Description: This file is a C callable C++ wrapper for 

   Developed by: Consumer system apps team, Bangalore, India

******************************************************************************/
#include <iostream>
#include <list>
#include <string>
#include <unordered_map>
extern "C" {
#include "a2b/pal.h"
#include "assert.h"
#include "a2b/ctypes.h"
#include "a2b/ctypes.h"
#include <a2bpnp.h>
#include <a2bpnp_local.h>
}
#include "a2bpnp_conn_mng.h"

extern "C" {
ConnectionMngClass connObj[4];
ChipIDNodeIDConv   idConvObj[4];

void connManageInit(int inst)
{
    connObj[inst].setIdConv(&idConvObj[inst]);
}

void connManageDeInit(int inst)
{
    connObj[inst].clearConnection();
}
void addConnection(int inst, int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup)
{
    connObj[inst].addConnection(srcNodeId, srcNodeGroup,dstNodeId, dstNodeGroup);
}

void addConnectionByChipID(int inst, connPairByChipId pair)
{
    connObj[inst].addConnectionByChipID(pair);
}

void removeConnection(int inst, int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup)
{
    connObj[inst].removeConnection(srcNodeId, srcNodeGroup,dstNodeId, dstNodeGroup);
}

void removeAll(int inst)
{
    connObj[inst].clearConnection();
}

void addNewNodeChipId(int inst, int nodeid, char chipId[], int nodeCnt)
{
    std::string chipIdStr;
    chipIdStr.assign(chipId,6);
    idConvObj[inst].addNode(nodeid, chipIdStr, nodeCnt);
}

void retrieveNWChipIds(int inst, char nwchipId[A2B_CONF_MAX_NUM_SLAVE_NODES+1][6], int nodeCnt)
{
    std::string chipIdStr;
    for(int i=0; i<nodeCnt; i++)
    {
        chipIdStr = idConvObj[inst].getChipId(i-1);
        const char* charArray = chipIdStr.c_str();

        for (int j = 0; j < (int)chipIdStr.size(); j++) 
        {
            nwchipId[i][j] = charArray[j];
        }
    }    //chipIdStr.assign(chipId,6);
}

void removeNodeId(int inst, int nodeid, int nodeCnt)
{
    idConvObj[inst].removeNode(nodeid,nodeCnt);
}
void getStreamFromConnection(int inst, a2bpnp_StreamInfo streamList[], int* num)
{
    connObj[inst].getStreamFromConnection(streamList, num,inst);
}

void getListOfConnection(int inst, connPairByChipId streamList[], uint8_t* num)
{
    connObj[inst].getListOfConnection(streamList, num);
}

void getListOfExistingConnection(int inst, connPairByChipId streamList[], uint8_t* num)
{
    connObj[inst].getListOfExistingConnection(streamList, num);
}
bool IsNodePartofAnyConnection(int inst, int nodeID)
{
    return(connObj[inst].IsNodePartofAnyConnection(nodeID));
}

int compareNodeName(char* tgtName, char* fromDevInfo)
{   
    int compareStatus = 0;
    for(int i = 0; i < 64; i++)
    {
        if((tgtName[i] == '\0') || (fromDevInfo[i] == '\0'))
        {
            return compareStatus;
        }
        else if(tgtName[i] == fromDevInfo[i])
        {
            compareStatus = 1;
        }
        else if(tgtName[i] != fromDevInfo[i])
        {
            compareStatus = 0;
        }
    }

    return compareStatus;
}

int getNumChannels(int inst, int nodeNum, int groupNo)
{
    return(getnumChannelsFromDevInfo(inst, nodeNum, groupNo));
}

}