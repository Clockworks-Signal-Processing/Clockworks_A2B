/*******************************************************************************
Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "adi_a2b_externs.h"
#include "assert.h"
#include <cmd_queue.h>
#include <cmd_platform.h>
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"
#include "a2bapp_test.h"
#include "cJSON-master/cJSON.h"

cJSON *cmd;
cJSON *name;
cJSON *feature;
cJSON *address;
cJSON *reg;
cJSON *value;

int JSONOpen(char* filename)
{

	//open log file
	FILE *file_js = fopen(filename, "r");
	if(file_js == NULL)
	{
        return -1;
	}

	//read JSON file
	fseek(file_js, 0, SEEK_END);
	long file_size = ftell(file_js);
	fseek(file_js, 0, SEEK_SET);
	char *json_buffer = (char *)malloc(file_size + 1);
	if (!json_buffer) {
		fclose(file_js);
		return -2;
	}
	if (fread(json_buffer, 1, file_size, file_js) != (size_t)file_size) {
		fclose(file_js);
		free(json_buffer);
		return -3;
	}
	else
	{
		//close JSON file
		fclose(file_js);
		json_buffer[file_size] = '\0';
	}
	// Parse the JSON data
	cJSON *root = cJSON_Parse(json_buffer);
	if (!root) {
		free(json_buffer);
        return -4;
	}
	// Access JSON data
	cmd = cJSON_GetObjectItem(root, "commands");


    return 0;
}


int JSONObjectMatch(cJSON *parentObj, char *objStr, char *str)
{
	int entity_count = cJSON_GetArraySize(parentObj);
	for (int i = 0; i < entity_count; i++) {
		cJSON* entity = cJSON_GetArrayItem(parentObj, i);
		if (cJSON_IsObject(entity)) {
			cJSON* name = cJSON_GetObjectItem(entity, objStr);
			if (name && cJSON_IsString(name) && (strcmp(name->valuestring, str) == 0)) {
				return 0;
			}
		}
	}
	return 1;
}

int a2bapp_verify_getNwInfo(void)
{
    int ret = JSONObjectMatch(cmd, "name","getNwInfo");
    //TODO: Parse features and verify 
}

int a2bapp_verify_ver(a2b_UInt32 major,a2b_UInt32 minor, a2b_UInt32 patch)
{
	if(nw[nwIdx].bTestRegression)
	{
		
	}
}