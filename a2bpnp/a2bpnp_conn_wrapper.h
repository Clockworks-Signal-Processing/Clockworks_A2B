#ifndef A2BPNP_CONN_WRAPPER_H
#define A2BPNP_CONN_WRAPPER_H

void connManageInit(int inst);
void connManageDeInit(int inst);
void addConnection(int inst, int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup);
void removeConnection(int inst, int srcNodeId, int srcNodeGroup, int dstNodeId, int dstNodeGroup);
void addNewNodeChipId(int inst, int nodeid, char chipId[], int nodeCnt);
void retrieveNWChipIds(int inst, char *nwchipId[], int nodeCnt);
void removeNodeId(int inst, int nodeid, int nodeCnt);
void removeAll(int inst);
void getStreamFromConnection(int inst, a2bpnp_StreamInfo streamList[], int* num);
void getListOfConnection(int inst, connPairByChipId streamList[], uint8_t* num);
int compareNodeName(char* tgtName, char* fromDevInfo);
int getNumChannels(int inst, int nodeNum, int groupNo);
void getListOfExistingConnection(int inst, connPairByChipId streamList[], uint8_t* num);
bool IsNodePartofAnyConnection(int inst, int nodeID);
void addConnectionByChipID(int inst, connPairByChipId pair);

#endif