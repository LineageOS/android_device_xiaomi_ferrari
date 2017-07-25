/*
 * Copyright (C) 2016, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0

#define LOG_TAG "wcnss_xiaomi"

#define MAC_ADDR_SIZE 6
#define WLAN_MAC_BIN "/persist/mac.wlan.bin"

#include <string.h>
#include <cutils/log.h>

const uint8_t xiaomi_oui_list[][3] =
{
	{ 0x9C, 0x99, 0xA0 },
	{ 0x18, 0x59, 0x36 },
	{ 0x98, 0xFA, 0xE3 },
	{ 0x64, 0x09, 0x80 },
	{ 0x8C, 0xBE, 0xBE },
	{ 0xF8, 0xA4, 0x5F },
	{ 0xC4, 0x0B, 0xCB },
	{ 0xEC, 0xD0, 0x9F },
	{ 0xE4, 0x46, 0xDA },
	{ 0xF4, 0xF5, 0xDB },
	{ 0x28, 0xE3, 0x1F },
	{ 0x0C, 0x1D, 0xAF },
	{ 0x14, 0xF6, 0x5A },
	{ 0x74, 0x23, 0x44 },
	{ 0xF0, 0xB4, 0x29 },
	{ 0xD4, 0x97, 0x0B },
	{ 0x64, 0xCC, 0x2E },
	{ 0xB0, 0xE2, 0x35 },
	{ 0x38, 0xA4, 0xED },
	{ 0xF4, 0x8B, 0x32 },
	{ 0x3C, 0xBD, 0x3E },
	{ 0x4C, 0x49, 0xE3 },
	{ 0x00, 0x9E, 0xC8 },
	{ 0xAC, 0xF7, 0xF3 },
	{ 0x10, 0x2A, 0xB3 },
	{ 0x58, 0x44, 0x98 },
	{ 0xA0, 0x86, 0xC6 },
	{ 0x7C, 0x1D, 0xD9 },
	{ 0x28, 0x6C, 0x07 },
	{ 0xAC, 0xC1, 0xEE },
	{ 0x78, 0x02, 0xF8 },
	{ 0x50, 0x8F, 0x4C },
	{ 0x68, 0xDF, 0xDD },
	{ 0xC4, 0x6A, 0xB7 },
	{ 0xFC, 0x64, 0xBA },
	{ 0x20, 0x82, 0xC0 },
	{ 0x34, 0x80, 0xB3 },
	{ 0x74, 0x51, 0xBA },
	{ 0x64, 0xB4, 0x73 },
	{ 0x34, 0xCE, 0x00 },
	{ 0x00, 0xEC, 0x0A },
	{ 0x78, 0x11, 0xDC },
	{ 0x50, 0x64, 0x2B },
};
const size_t xiaomi_oui_list_size = sizeof(xiaomi_oui_list) / 3;

static int check_wlan_mac_bin_file(uint8_t buf[]) {
	char content[6+1];
	FILE* fp;
	size_t i;

	fp = fopen(WLAN_MAC_BIN, "r");
	if (fp == NULL)
		return -1;

	memset(content, 0, sizeof(content));
	fread(content, 1, sizeof(content) - 1, fp);
	fclose(fp);

	for (i = 0; i < xiaomi_oui_list_size; i++) {
		if(content[0] == xiaomi_oui_list[i][0] && content[1] == xiaomi_oui_list[i][1] && content[2] == xiaomi_oui_list[i][2]) {
			memcpy(buf, content, MAC_ADDR_SIZE);
			return 0;
		}
	}

	return -1;
}

int wcnss_init_qmi(void)
{
	/* empty */
	return 0;
}

int wcnss_qmi_get_wlan_address(unsigned char *pWlanAddr)
{
	int rc;
	uint8_t buf[MAC_ADDR_SIZE];

	rc = check_wlan_mac_bin_file(buf);
	if (rc) {
		return rc;
	}

	memcpy(pWlanAddr, buf, MAC_ADDR_SIZE);

	ALOGI("Found MAC address: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n",
			pWlanAddr[0],
			pWlanAddr[1],
			pWlanAddr[2],
			pWlanAddr[3],
			pWlanAddr[4],
			pWlanAddr[5]);

	return 0;
}

void wcnss_qmi_deinit(void)
{
	/* empty */
}
