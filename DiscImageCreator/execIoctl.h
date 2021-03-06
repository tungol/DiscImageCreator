/**
 * Copyright 2011-2018 sarami
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

BOOL DiskGetMediaTypes(
	PDEVICE pDevice,
	LPCTSTR pszPath
);

BOOL ScsiGetAddress(
	PDEVICE pDevice
);

BOOL ScsiPassThroughDirect(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	LPVOID lpCdbCmd,
	BYTE byCdbCmdLength,
	LPVOID pvBuffer,
	DWORD dwBufferLength,
	LPBYTE byScsiStatus,
	LPCTSTR pszFuncName,
	LONG lLineNum
);

BOOL StorageQueryProperty(
	PDEVICE pDevice,
	LPBOOL lpBusTypeUSB
);

BOOL SetStreaming(
	PDEVICE pDevice,
	DWORD dwDiscSpeedNum
);

BOOL DvdStartSession(
	PDEVICE pDevice,
	PDVD_COPY_PROTECT_KEY dvdKey
);

BOOL ReadKey(
	PDEVICE pDevice,
	PDVD_COPY_PROTECT_KEY dvdKey
);

BOOL SendKey(
	PDEVICE pDevice,
	PDVD_COPY_PROTECT_KEY dvdKey
);
