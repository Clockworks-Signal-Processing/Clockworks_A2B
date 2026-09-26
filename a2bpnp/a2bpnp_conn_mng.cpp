/*******************************************************************************
Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bpnp_conn_mng.cpp

   Description: This file is responsible for handling stream set up using the chip ID and preserving over the session

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
#include "a2bpnp_conn_wrapper.h"
}
#include "a2bpnp_conn_mng.h"


ConnectionPairClass::ConnectionPairClass(connPairByChipId pair)
{
  connectionPair.dst = pair.dst;
  connectionPair.src = pair.src;
}

ConnectionPairClass::ConnectionPairClass()
{

}

bool ConnectionPairClass::compareDst(ConnectionPairClass other)
{
    //connection check
  if( connectionPair.dst.grpNo != other.connectionPair.dst.grpNo)
  {
    return false;
  }
  for(int i = 0;i < 6;i++)
  {
    if( connectionPair.dst.chipid[i] != other.connectionPair.dst.chipid[i])
    {
        return false;
    }
  }
  return true;
}

//Checks for equality
bool ConnectionPairClass::operator==( const ConnectionPairClass& rhs) const
{
  //connection check
  if( connectionPair.dst.grpNo != rhs.connectionPair.dst.grpNo)
  {
    return false;
  }
  for(int i = 0;i < 6;i++)
  {
    if( connectionPair.dst.chipid[i] != rhs.connectionPair.dst.chipid[i])
    {
        return false;
    }
  }

  if( connectionPair.src.grpNo != rhs.connectionPair.src.grpNo)
  {
    return false;
  }
  for(int i = 0;i < 6;i++)
  {
    if( connectionPair.src.chipid[i] != rhs.connectionPair.src.chipid[i])
    {
        return false;
    }
  }
  return true;

}

void ConnectionPairClass::addSrc(std::string chipId, int grpNo)
{
    connectionPair.src.grpNo = grpNo;
    for(int i = 0;i < 6; i++)
    {
      connectionPair.src.chipid[i] = chipId[i];
    }
}

void ConnectionPairClass::addDst(std::string chipId, int grpNo)
{
    connectionPair.dst.grpNo = grpNo;
    for(int i = 0;i < 6; i++)
    {
      connectionPair.dst.chipid[i] = chipId[i];
    }
}


  //Checks for equality
  int ChipIDNodeIDConv::getNodeNum(std::string chipId)
  {
    int nodeId = -2;
    for(int i = 0;i<nodecnt;i++)
    {
      if(chipIdByNodeID[i] == chipId)
      {
        return --i;
      }
    }

    return nodeId;
    
  }

  std::string ChipIDNodeIDConv::getChipId(int nodeId)
  {
    return (chipIdByNodeID[nodeId + 1]);
  }

  void ChipIDNodeIDConv::addNode(int nodeId, std::string chipId, int nodeCnt)
  {
    chipIdByNodeID[nodeId + 1] = chipId;
    nodecnt = nodeCnt;
  }

  void ChipIDNodeIDConv::removeNode(int nodeId, int nodeCnt)
  {
    nodecnt = nodeCnt;

    for(int i = nodeId; i < 17;i++)
    {
      chipIdByNodeID[nodeId + 1] = "";
    }

  }




    // Constructor
    ConnectionMngClass::ConnectionMngClass(ChipIDNodeIDConv* convset) {
        // Initialize the list with some default values
        conv = convset;
        connectionList.clear();
    }

    ConnectionMngClass::ConnectionMngClass()
    {

    }

    void ConnectionMngClass::setIdConv(ChipIDNodeIDConv *convset)
    {
      conv = convset;
    }

    // Add a string to the list
    void ConnectionMngClass::addConnection(const ConnectionPairClass& str) {
        connectionList.push_back(str);
    }

    void ConnectionMngClass::addConnectionByChipID(connPairByChipId pair)
    {
      ConnectionPairClass conObj(pair);
      bool exits = false;
      for (auto it = connectionList.begin(); it != connectionList.end(); ++it)
      {
            if(*it == conObj)
            {
              exits = true;
            }
            else if(it->compareDst(conObj))
            {
              //remove the old connection
              connectionList.remove(*it);
              break;
            }
      }
      if(exits == false)
      {
        connectionList.push_back(conObj);
      }

    }
    void ConnectionMngClass::addConnection(int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup)
    {
      ConnectionPairClass conObj;
      std::string chipid_src, chipid_dst;
      chipid_src = conv->getChipId(srcNodeId);
      chipid_dst = conv->getChipId(dstNodeId);
      conObj.addSrc(chipid_src,  srcNodeGroup);
      conObj.addDst(chipid_dst,dstNodeGroup);
      bool exits = false;
      for (auto it = connectionList.begin(); it != connectionList.end(); ++it)
      {
            if(*it == conObj)
            {
              exits = true;
            }
            else if(it->compareDst(conObj))
            {
              //remove the old connection
              connectionList.remove(*it);
              break;
            }
      }
      if(exits == false)
      {
        connectionList.push_back(conObj);
      }

    }

    void ConnectionMngClass::addConnection(connPairByChipId pair)
    {
      ConnectionPairClass conObj(pair);
    }
        // Add a string to the list
    void ConnectionMngClass::clearConnection() {
        connectionList.clear();
    }

    // Remove a string from the list
    void ConnectionMngClass::removeConnection( const ConnectionPairClass& str) {
        connectionList.remove(str);
    }

    void ConnectionMngClass::removeConnection(int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup)
    {
      ConnectionPairClass conObj;
      std::string chipid_src, chipid_dst;
      chipid_src = conv->getChipId(srcNodeId);
      chipid_dst = conv->getChipId(dstNodeId);
      conObj.addSrc(chipid_src,  srcNodeGroup);
      conObj.addDst(chipid_dst,dstNodeGroup);
      connectionList.remove(conObj);

    }

    void ConnectionMngClass::getStreamFromConnection(a2bpnp_StreamInfo streamList[], int* num, int inst)
    {
      std::string chipid_src, chipid_dst;
      int nodeId_src, nodeId_dst;
      int idx = 0, idxCur;
      std::unordered_map<std::string, int> stremIdxByChipId;
      int destCntByStreamIDx[16] = {0};

      for (auto it = connectionList.begin(); it != connectionList.end(); ++it) {
           chipid_src.assign(it->connectionPair.src.chipid,6);
           nodeId_src = conv->getNodeNum(chipid_src);

           chipid_dst.assign(it->connectionPair.dst.chipid,6);
           nodeId_dst = conv->getNodeNum(chipid_dst);
           //This connection does not exist
           if( (nodeId_src != -2) && (nodeId_dst != -2) )
           {
             std::string val = chipid_src + std::to_string(it->connectionPair.src.grpNo);
              if(stremIdxByChipId.count(val) == 0)
              {
                streamList[idx].SourceNodeIdx = nodeId_src;
                streamList[idx].srcRxGroup = it->connectionPair.src.grpNo;
                streamList[idx].DestNodeIdx[destCntByStreamIDx[idx]] = nodeId_dst;
                streamList[idx].destTxGroup[destCntByStreamIDx[idx]] = it->connectionPair.dst.grpNo;
                streamList[idx].nDestNodes = 1;
                //TODO: Change number of channels
                streamList[idx].nNumChannels = getNumChannels(inst,nodeId_src,it->connectionPair.src.grpNo ); //Get it from source node ID
                streamList[idx].streamIdx = idx;

                //Add New entry into the dictionary
                stremIdxByChipId[val] = idx;
                
                destCntByStreamIDx[idx]++;
                idx++;

              }
              else
              {
                idxCur = stremIdxByChipId[val];
                streamList[idxCur].DestNodeIdx[destCntByStreamIDx[idxCur]] = nodeId_dst;
                streamList[idxCur].destTxGroup[destCntByStreamIDx[idxCur]] = it->connectionPair.dst.grpNo;
                destCntByStreamIDx[idxCur]++;
                streamList[idxCur].nDestNodes++;
              }
           }
        }
        *num = idx;
    }

    void ConnectionMngClass::getListOfConnection(connPairByChipId connPairList[], uint8_t* num)
    {
      int idx = 0;
      for (auto it = connectionList.begin(); it != connectionList.end(); ++it) {
          connPairList[idx] = it->connectionPair;
          idx++;
        }
        *num = idx;
    }

    void ConnectionMngClass::getListOfExistingConnection(connPairByChipId connPairList[], uint8_t* num)
    {

      std::string chipid_src, chipid_dst;
      int nodeId_src, nodeId_dst;
      int idx = 0;
      std::unordered_map<std::string, int> stremIdxByChipId;

      for (auto it = connectionList.begin(); it != connectionList.end(); ++it) {
           chipid_src.assign(it->connectionPair.src.chipid,6);
           nodeId_src = conv->getNodeNum(chipid_src);

           chipid_dst.assign(it->connectionPair.dst.chipid,6);
           nodeId_dst = conv->getNodeNum(chipid_dst);
           //This connection does not exist
           if( (nodeId_src != -2) && (nodeId_dst != -2) )
           {
              connPairList[idx] = it->connectionPair;
              idx++;
           }
        }
        *num = idx;
    }

    bool ConnectionMngClass::IsNodePartofAnyConnection( int nodeID)
    {

      std::string chipid_src, chipid_dst;
      int nodeId_src, nodeId_dst;
      std::unordered_map<std::string, int> stremIdxByChipId;
      bool res = false; 

      for (auto it = connectionList.begin(); it != connectionList.end(); ++it) {
           chipid_src.assign(it->connectionPair.src.chipid,6);
           nodeId_src = conv->getNodeNum(chipid_src);

           chipid_dst.assign(it->connectionPair.dst.chipid,6);
           nodeId_dst = conv->getNodeNum(chipid_dst);
           //This connection does not exist
           if( (nodeId_src != -2) && (nodeId_dst != -2) )
           {
              if( (nodeID == nodeId_src) || (nodeID == nodeId_dst))
              {
                res = true;
                break;
              } 
           }
        }

        return res;

    }

    void ConnectionMngClass::setConnectionFromStream(a2bpnp_StreamInfo streamList[], int num)
    {
        connectionList.clear();
        ConnectionPairClass conObj;

        for(int i = 0; i < num; i++)
        {
          conObj.addSrc(conv->getChipId(streamList[i].SourceNodeIdx),  streamList[i].srcRxGroup);

          for(int j = 0; j < streamList[i].nDestNodes;j++ )
          {
            conObj.addDst(conv->getChipId(streamList[i].DestNodeIdx[j]), streamList[i].destTxGroup[j]);
            connectionList.push_back(conObj);
          }
        }
    }



