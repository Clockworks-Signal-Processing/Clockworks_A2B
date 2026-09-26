#ifndef A2BPNP_CONN_MNG_H
#define A2BPNP_CONN_MNG_H

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

class ConnectionPairClass {
public:
    connPairByChipId connectionPair;

public:
ConnectionPairClass(connPairByChipId pair);
ConnectionPairClass();
//Checks for equality
bool operator==( const ConnectionPairClass& rhs) const;
void addSrc(std::string chipId, int grpNo);
void addDst(std::string chipId, int grpNo);
bool compareDst(ConnectionPairClass other);

};

class ChipIDNodeIDConv {
public:
    std::string chipIdByNodeID[17];
    int nodecnt = 1;
public:

  //Checks for equality
  int getNodeNum(std::string chipId);
  std::string getChipId(int nodeId);
  void addNode(int nodeId, std::string chipId, int nodeCnt);
  void removeNode(int nodeId , int nodeCnt);

};

class ConnectionMngClass {
private:
    std::list<ConnectionPairClass> connectionList;
    ChipIDNodeIDConv *conv;

public:
    // Constructor
    ConnectionMngClass(ChipIDNodeIDConv* convset);
    ConnectionMngClass();
    void setIdConv(ChipIDNodeIDConv* convset);

    // Add a string to the list
    void addConnection(const ConnectionPairClass& str);
    
    void addConnection(int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup);

    void addConnection(connPairByChipId pair);

    void addConnectionByChipID(connPairByChipId pair);

    // Add a string to the list
    void clearConnection();

    // Remove a string from the list
    void removeConnection( const ConnectionPairClass& str);

    void removeConnection(int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup);

    void getStreamFromConnection(a2bpnp_StreamInfo streamList[], int* num, int inst);
    void getListOfConnection(connPairByChipId streamList[], uint8_t* num);
    void setConnectionFromStream(a2bpnp_StreamInfo streamList[], int num);
    void getListOfExistingConnection(connPairByChipId connPairList[], uint8_t* num);
    bool IsNodePartofAnyConnection( int nodeID);


};

#endif