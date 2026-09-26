/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************/
#include <Windows.h>
#include <SetupAPI.h>
#include <stdio.h>
#include <stdlib.h>

int usb_drive_sel() {
    // Define the USB class GUID
    GUID usbClassGuid = {0x36fc9e60, 0xc465, 0x11cf, {0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

    // Initialize the SetupAPI library and obtain a handle to the device information set
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&usbClassGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        printf("Failed to initialize SetupAPI.\n");
        return 1;
    }

    // Enumerate through the USB devices
    DWORD index = 0;
    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    while (SetupDiEnumDeviceInfo(deviceInfoSet, index, &deviceInfoData)) {
        // Get the class GUID for the current device
        GUID deviceClassGuid;
        if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData, SPDRP_CLASSGUID, NULL, (BYTE*)&deviceClassGuid, sizeof(GUID), NULL)) {
            if (IsEqualGUID(&deviceClassGuid, &usbClassGuid)) {
                // Set the driver for the matched device
                if (SetupDiSetSelectedDevice(deviceInfoSet, &deviceInfoData)) {
                    printf("Driver set successfully for the matched USB device.\n");
                } else {
                    printf("Failed to set the driver.\n");
                }
            }
        }

        index++;
    }

    // Clean up
    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    return 0;
}

#include <windows.h>
#include <stdio.h>

#ifndef GUID_DEVCLASS_USB
DEFINE_GUID(GUID_DEVCLASS_USB, 0x36FC9E60, 0xC465, 0x11CF, 0x80, 0x00, 0x00, 0xAA, 0x00, 0xB5, 0x69, 0x6F);
#endif

#ifndef INSTALLFLAG_FORCE
#define INSTALLFLAG_FORCE 0x00000001
#endif


int driver_switch() {
    // Specify the device instance ID of the USB device
    const char* deviceInstanceID = "USB\\VID_1234&PID_5678\\0123456789ABCDEF";

    // Specify the target driver INF file path
    const char* targetDriverInfPath = "C:\\path\\to\\driver.inf";

    // Get a handle to the device information set for USB devices
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&GUID_DEVCLASS_USB, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        printf("Failed to get device information set.\n");
        return 1;
    }

    // Prepare the device interface data structure
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData = { 0 };
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Enumerate through the device interfaces to find the matching device instance ID
    DWORD deviceIndex = 0;
    while (SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVCLASS_USB, deviceIndex, &deviceInterfaceData)) {
        // Get the required buffer size for the device path
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);

        // Allocate memory for the device path
        PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        // Get the device path
        if (SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, deviceInterfaceDetailData, requiredSize, NULL, NULL)) {
            // Check if the device instance ID matches
            if (strstr(deviceInterfaceDetailData->DevicePath, deviceInstanceID) != NULL) {
                // Open a handle to the device
                HANDLE deviceHandle = CreateFileA(deviceInterfaceDetailData->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (deviceHandle != INVALID_HANDLE_VALUE) {
                    // Install the target driver for the device
                if (SetupCopyOEMInfA(targetDriverInfPath, NULL, SPOST_PATH, 0, NULL, 0, NULL, NULL)) {
                    printf("Driver installed successfully.\n");
                } else {
                    printf("Failed to install driver.\n");
                }

                    // Close the device handle
                    CloseHandle(deviceHandle);
                } else {
                    printf("Failed to open device.\n");
                }

                // Free the allocated memory
                free(deviceInterfaceDetailData);

                // Stop enumerating devices
                break;
            }
        }

        // Free the allocated memory
        free(deviceInterfaceDetailData);

        // Move to the next device interface
        deviceIndex++;
    }

    // Destroy the device information set
    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    return 0;
}
