/*******************************************************************************
Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************/
#ifndef __A2BAPP_TEST_H_
#define __A2BAPP_TEST_H_

#include "cJSON-master/cJSON.h"

int JSONOpen(char* filename);
int JSONObjectMatch(cJSON *parentObj, char *objStr, char *str);
int a2bapp_verify_getNwInfo(void);

#endif