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
#include "struct.h"
#include "check.h"
#include "convert.h"
#include "execIoctl.h"
#include "execScsiCmd.h"
#include "execScsiCmdforCD.h"
#include "execScsiCmdforCDCheck.h"
#include "init.h"
#include "get.h"
#include "output.h"
#include "outputScsiCmdLog.h"
#include "outputScsiCmdLogforCD.h"
#include "set.h"

BOOL TestUnitReady(
	PEXT_ARG pExtArg,
	PDEVICE pDevice
) {
	CDB::_CDB6GENERIC cdb = { 0 };
	cdb.OperationCode = SCSIOP_TEST_UNIT_READY;
	cdb.LogicalUnitNumber = pDevice->address.Lun;

	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB6GENERIC_LENGTH,
		NULL, 0, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		return FALSE;
	}
	return TRUE;
}

BOOL Inquiry(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice
) {
	CDB::_CDB6INQUIRY3 cdb = { 0 };
	cdb.OperationCode = SCSIOP_INQUIRY;
	cdb.AllocationLength = sizeof(INQUIRYDATA);

	_declspec(align(4)) INQUIRYDATA inquiryData = { 0 };
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB6GENERIC_LENGTH, &inquiryData, 
		sizeof(INQUIRYDATA), &byScsiStatus, _T(__FUNCTION__), __LINE__) ||
		byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		return FALSE;
	}
	strncpy(pDevice->szVendorId,
		(LPCH)&inquiryData.VendorId, sizeof(pDevice->szVendorId));
	strncpy(pDevice->szProductId,
		(LPCH)&inquiryData.ProductId, sizeof(pDevice->szProductId));
	strncpy(pDevice->szProductRevisionLevel,
		(LPCH)&inquiryData.ProductRevisionLevel, sizeof(pDevice->szProductRevisionLevel));

	if (*pExecType != drivespeed) {
		OutputInquiry(&inquiryData);
	}
	return TRUE;
}

BOOL ModeSense(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc
) {
	if (pDevice->FEATURE.byModePage2a) {
		CDB::_MODE_SENSE cdb = { 0 };
		cdb.OperationCode = SCSIOP_MODE_SENSE;
#if (NTDDI_VERSION <= NTDDI_WIN7)
		cdb.LogicalUnitNumber = pDevice->address.Lun;
#endif
		cdb.PageCode = MODE_PAGE_CAPABILITIES;
		cdb.Pc = 2;// MODE_SENSE_CURRENT_VALUES;
		cdb.AllocationLength = sizeof(CDVD_CAPABILITIES_PAGE_WITH_HEADER);

		_declspec(align(4)) CDVD_CAPABILITIES_PAGE_WITH_HEADER modesense = { 0 };
		BYTE byScsiStatus = 0;
		if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB6GENERIC_LENGTH, &modesense,
			sizeof(CDVD_CAPABILITIES_PAGE_WITH_HEADER), &byScsiStatus, _T(__FUNCTION__), __LINE__) ||
			byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			// not false.
		}
		else {
			if (modesense.cdvd.PageCode == MODE_PAGE_CAPABILITIES) {
				WORD rsm = MAKEWORD(modesense.cdvd.ReadSpeedMaximum[1],
					modesense.cdvd.ReadSpeedMaximum[0]);
				INT perKb = 176;
				if (pDisc->SCSI.wCurrentMedia == ProfileDvdRom) {
					perKb = 1385;
				}
				else if (pDisc->SCSI.wCurrentMedia == ProfileBDRom) {
					perKb = 4496;
				}
				if (*pExecType == drivespeed) {
					OutputString("ReadSpeedMaximum: %uKB/sec (%ux)\n", rsm, rsm / perKb);
				}
				else {
					pDevice->wMaxReadSpeed = rsm;
					OutputModeParmeterHeader(&modesense.header);
					OutputCDVDCapabilitiesPage(&modesense.cdvd, perKb);
				}
			}
			else {
				OutputDriveLogA(
					"SCSIOP_MODE_SENSE didn't fail. But it couldn't get PageCode on this drive\n");
			}
		}
	}
	return TRUE;
}

BOOL StartStopUnit(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	BYTE Start,
	BYTE LoadEject
) {
	CDB::_START_STOP cdb = { 0 };
	cdb.OperationCode = SCSIOP_START_STOP_UNIT;
	cdb.LogicalUnitNumber = pDevice->address.Lun;
	cdb.Start = Start;
	cdb.LoadEject = LoadEject;

	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB6GENERIC_LENGTH,
		NULL, 0, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		return FALSE;
	}
	return TRUE;
}

BOOL ReadTOC(
	PEXT_ARG pExtArg,
	PEXEC_TYPE pExecType,
	PDEVICE pDevice,
	PDISC pDisc
) {
	CDB::_READ_TOC cdb = { 0 };
	cdb.OperationCode = SCSIOP_READ_TOC;
	cdb.LogicalUnitNumber = pDevice->address.Lun;
	cdb.Format2 = CDROM_READ_TOC_EX_FORMAT_TOC;
	cdb.StartingTrack = 1;
	WORD wSize = CDROM_TOC_SIZE;
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

#ifdef _DEBUG
	OutputString(_T("pDisc->SCSI.toc address: %p\n"), &pDisc->SCSI.toc);
#endif
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
		, &pDisc->SCSI.toc,	wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__) ||
		byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		if (pDisc->SCSI.wCurrentMedia != ProfileDvdRom) {
			return FALSE;
		}
	}
	if (pDisc->SCSI.toc.FirstTrack < 1 || 99 < pDisc->SCSI.toc.FirstTrack ||
		pDisc->SCSI.toc.LastTrack < 1 || 99 < pDisc->SCSI.toc.LastTrack) {
		OutputErrorString(_T("Invalid TOC\n"));
		return FALSE;
	}
	if (!InitLBAPerTrack(pExecType, &pDisc)) {
		return FALSE;
	}
	if (byScsiStatus == SCSISTAT_GOOD) {
		pDevice->bySuccessReadToc = TRUE;
		SetAndOutputToc(pDisc);
		if (*pExecType == gd) {
			pDisc->SCSI.trackType = TRACK_TYPE::dataExist;
			OutputDiscLogA("This is the TOC of audio trap disc\n");
		}
	}
	return TRUE;
}

BOOL ReadTOCFull(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	PDISC_PER_SECTOR pDiscPerSector,
	FILE* fpCcd
) {
	CDB::_READ_TOC cdb = { 0 };
	cdb.OperationCode = SCSIOP_READ_TOC;
	cdb.LogicalUnitNumber = pDevice->address.Lun;
	cdb.Format2 = CDROM_READ_TOC_EX_FORMAT_FULL_TOC;
	cdb.StartingTrack = 1;
	WORD wSize = sizeof(CDROM_TOC_FULL_TOC_DATA);
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

	_declspec(align(4)) CDROM_TOC_FULL_TOC_DATA fullToc = { 0 };
#ifdef _DEBUG
	OutputString(_T("fullToc address: %p\n"), &fullToc);
#endif
	LPBYTE pBuf = NULL;
	LPBYTE lpBuf = NULL;
	BYTE lpCmd[CDB12GENERIC_LENGTH] = { 0 };
	INT nOfs = 0;
	BYTE byMode = DATA_BLOCK_MODE0;
	BYTE bySessionNum = 0;
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
		, &fullToc,	wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		DWORD dwBufLen = CD_RAW_SECTOR_SIZE + CD_RAW_READ_SUBCODE_SIZE;
		if (!ReadCDForCheckingSubQAdrFirst(pExtArg, pDevice
			, pDisc, &pBuf, &lpBuf, lpCmd, &dwBufLen, &nOfs, CDFLAG::_READ_CD::All)) {
			return FALSE;
		}
		for (BYTE i = 0; i < pDisc->SCSI.toc.LastTrack; i++) {
			if (!ReadCDForCheckingSubQAdr(pExtArg, pDevice, pDisc
				, pDiscPerSector, lpCmd, lpBuf, dwBufLen, nOfs, i, &byMode, 1, fpCcd)) {
				return FALSE;
			}
			if (bySessionNum < 1) {
				WriteCcdForSession(1, byMode, fpCcd);
				bySessionNum++;
			}
			OutputString(
				_T("\rChecking SubQ adr (Track) %2u/%2u"), i + 1, pDisc->SCSI.toc.LastTrack);
		}
		OutputString(_T("\n"));
		pDevice->bySuccessReadTocFull = FALSE;
		return TRUE;
	}
	WORD wFullTocLen = MAKEWORD(fullToc.Length[1], fullToc.Length[0]);
	WORD wTocEntriesAll = wFullTocLen - sizeof(fullToc.Length);
	WORD wTocEntries = wTocEntriesAll / sizeof(CDROM_TOC_FULL_TOC_DATA_BLOCK);

	if (fpCcd) {
		WriteCcdForDisc(wTocEntries, fullToc.LastCompleteSession, fpCcd);
		if (pDevice->FEATURE.byCanCDText) {
			ReadTOCText(pExtArg, pDevice, pDisc, fpCcd);
		}
	}
	pDisc->SCSI.bMultiSession = fullToc.LastCompleteSession > 1 ? TRUE : FALSE;

	WORD wFullTocLenFix = wTocEntriesAll + sizeof(CDROM_TOC_FULL_TOC_DATA);
	// 4 byte padding
	if (wFullTocLenFix % 4) {
		wFullTocLenFix = (WORD)((wFullTocLenFix / 4 + 1) * 4);
	}
	LPBYTE pPFullToc = NULL;
	LPBYTE pFullToc = NULL;
	if (!GetAlignedCallocatedBuffer(pDevice, &pPFullToc,
		wFullTocLenFix, &pFullToc, _T(__FUNCTION__), __LINE__)) {
		return FALSE;
	}
#ifdef _DEBUG
	OutputDiscLogA(
		"FullTocLen: %u, TocEntriesAll: %u, TocEntries: %u, FullTocLenFix: %u\n",
		wFullTocLen, wTocEntriesAll, wTocEntries, wFullTocLenFix);
#endif
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wFullTocLenFix);
	BOOL bRet = TRUE;
	try {
		if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
			, pFullToc,	wFullTocLenFix, &byScsiStatus, _T(__FUNCTION__), __LINE__)
			|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			throw FALSE;
		}
		PCDROM_TOC_FULL_TOC_DATA_BLOCK pTocData =
			((PCDROM_TOC_FULL_TOC_DATA)pFullToc)->Descriptors;
		DWORD dwBufLen = CD_RAW_SECTOR_SIZE + CD_RAW_READ_SUBCODE_SIZE;
		if (!ReadCDForCheckingSubQAdrFirst(pExtArg
			, pDevice, pDisc, &pBuf, &lpBuf, lpCmd, &dwBufLen, &nOfs, CDFLAG::_READ_CD::All)) {
			throw FALSE;
		}
		for (WORD i = 0; i < wTocEntries; i++) {
			if (pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX40TS) {
				// Somehow Ultraplex seems to get the fulltoc data as "hexadecimal"
				pTocData[i].Msf[0] = BcdToDec(pTocData[i].Msf[0]);
				pTocData[i].Msf[1] = BcdToDec(pTocData[i].Msf[1]);
				pTocData[i].Msf[2] = BcdToDec(pTocData[i].Msf[2]);
				pTocData[i].MsfExtra[0] = BcdToDec(pTocData[i].MsfExtra[0]);
				pTocData[i].MsfExtra[1] = BcdToDec(pTocData[i].MsfExtra[1]);
				pTocData[i].MsfExtra[2] = BcdToDec(pTocData[i].MsfExtra[2]);
				if (pTocData[i].Point < 0xa0) {
					pTocData[i].Point = BcdToDec(pTocData[i].Point);
				}
			}
			if (pTocData[i].Point < 100) {
				if (!ReadCDForCheckingSubQAdr(pExtArg, pDevice, pDisc, pDiscPerSector, lpCmd, lpBuf
					, dwBufLen, nOfs, (BYTE)(pTocData[i].Point - 1), &byMode, pTocData[i].SessionNumber, fpCcd)) {
					throw FALSE;
				}
				if (bySessionNum < pTocData[i].SessionNumber) {
					WriteCcdForSession(pTocData[i].SessionNumber, byMode, fpCcd);
					bySessionNum++;
				}
				OutputString(
					_T("\rChecking SubQ adr (Track) %2u/%2u"), pTocData[i].Point, pDisc->SCSI.toc.LastTrack);
			}
		}
		OutputString(_T("\n"));
		SetAndOutputTocFull(pDisc, &fullToc, pTocData, wTocEntries, fpCcd);
	}
	catch (BOOL ret) {
		bRet = ret;
	}
	pDevice->bySuccessReadTocFull = TRUE;
	FreeAndNull(pPFullToc);
	FreeAndNull(pBuf);
	return bRet;
}

BOOL ReadTOCAtip(
	PEXT_ARG pExtArg,
	PDEVICE pDevice
) {
	CDB::_READ_TOC cdb = { 0 };
	cdb.OperationCode = SCSIOP_READ_TOC;
	cdb.LogicalUnitNumber = pDevice->address.Lun;
	cdb.Format2 = CDROM_READ_TOC_EX_FORMAT_ATIP;
	WORD wSize = sizeof(CDROM_TOC_ATIP_DATA);
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

	_declspec(align(4)) CDROM_TOC_ATIP_DATA atip = { 0 };
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
		, &atip, wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		OutputDriveNoSupportLogA(READ_TOC_ATIP);
		return TRUE;
	}
	WORD wTocAtipLen = MAKEWORD(atip.Length[1], atip.Length[0]);
	WORD wTocAtipAll = wTocAtipLen - sizeof(atip.Length);

	LPBYTE pPTocAtip = NULL;
	LPBYTE pTocAtip = NULL;
	if (!GetAlignedCallocatedBuffer(pDevice, &pPTocAtip,
		wTocAtipAll, &pTocAtip, _T(__FUNCTION__), __LINE__)) {
		OutputDriveNoSupportLogA(READ_TOC_ATIP);
		return TRUE;
	}
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wTocAtipAll);
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
		, pTocAtip, wTocAtipAll, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		OutputDriveNoSupportLogA(READ_TOC_ATIP);
		return TRUE;
	}
	PCDROM_TOC_ATIP_DATA_BLOCK pDesc =
		((PCDROM_TOC_ATIP_DATA)pTocAtip)->Descriptors;
	OutputCDAtip(pDesc);
	FreeAndNull(pPTocAtip);
	return TRUE;
}

BOOL ReadTOCText(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	FILE* fpCcd
) {
	CDB::_READ_TOC cdb = { 0 };
	cdb.OperationCode = SCSIOP_READ_TOC;
	cdb.LogicalUnitNumber = pDevice->address.Lun;
	cdb.Format2 = CDROM_READ_TOC_EX_FORMAT_CDTEXT;
	WORD wSize = sizeof(CDROM_TOC_CD_TEXT_DATA);
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

	_declspec(align(4)) CDROM_TOC_CD_TEXT_DATA tocText = { 0 };
#ifdef _DEBUG
	OutputString(_T("tocText address: %p\n"), &tocText);
#endif
	OutputDiscLogA(OUTPUT_DHYPHEN_PLUS_STR(CDTEXT));
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
		, &tocText,	wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		// not false. because undefined mmc1..
		OutputDriveNoSupportLogA(Nothing CDTEXT or READ_TOC_EX_FORMAT_CDTEXT);
		return TRUE;
	}
	WORD wTocTextLen = MAKEWORD(tocText.Length[1], tocText.Length[0]);
	WORD wTocTextEntriesAll = wTocTextLen - sizeof(tocText.Length);

	WriteCcdForDiscCDTextLength(wTocTextEntriesAll, fpCcd);
	if (!wTocTextEntriesAll) {
		OutputDiscLogA("\tNothing\n");
		// many CD is no text
		return TRUE;
	}

	WORD wTocTextEntries = wTocTextEntriesAll / sizeof(CDROM_TOC_CD_TEXT_DATA_BLOCK);
	WriteCcdForCDText(wTocTextEntries, fpCcd);

	WORD wTocTextLenFix = wTocTextEntriesAll + sizeof(CDROM_TOC_CD_TEXT_DATA);
	// 4 byte padding
	if (wTocTextLenFix % 4) {
		wTocTextLenFix = (WORD)((wTocTextLenFix / 4 + 1) * 4);
	}
	LPBYTE pPTocText = NULL;
	LPBYTE pTocText = NULL;
	if (!GetAlignedCallocatedBuffer(pDevice, &pPTocText,
		wTocTextLenFix, &pTocText, _T(__FUNCTION__), __LINE__)) {
		return FALSE;
	}
#ifdef _DEBUG
	OutputDiscLogA(
		"TocTextLen: %u, TocTextEntriesAll: %u, TocTextEntries: %u, TocTextLenFix: %u\n",
		wTocTextLen, wTocTextEntriesAll, wTocTextEntries, wTocTextLenFix);
	OutputString(_T("pPTocText address: %p\n"), &pPTocText);
	OutputString(_T("pTocText address: %p\n"), &pTocText);
#endif
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wTocTextLenFix);
	LPCH pTmpText = NULL;
	BOOL bRet = TRUE;
	try {
		if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
			, pTocText,	wTocTextLenFix, &byScsiStatus, _T(__FUNCTION__), __LINE__)
			|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			throw FALSE;
		}
		PCDROM_TOC_CD_TEXT_DATA_BLOCK pDesc =
			((PCDROM_TOC_CD_TEXT_DATA)pTocText)->Descriptors;
		WriteCcdForCDTextEntry(pDesc, wTocTextEntries, fpCcd);

		WORD wAllTextSize = wTocTextEntries * sizeof(pDesc->Text);
		if (NULL == (pTmpText = (LPCH)calloc(wAllTextSize, sizeof(_TCHAR)))) {
			OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
			throw FALSE;
		}
		WORD wEntrySize = 0;
		BOOL bUnicode = FALSE;
		while (wEntrySize < wTocTextEntries) {
			if (pDesc[wEntrySize].Unicode == 1) {
				bUnicode = TRUE;
				break;
			}
			wEntrySize++;
		}
		SetAndOutputTocCDText(pDisc, pDesc, pTmpText, wEntrySize, wAllTextSize);
		if (bUnicode) {
			PWCHAR pTmpWText = NULL;
			if (NULL == (pTmpWText = (PWCHAR)calloc(wAllTextSize, sizeof(BYTE)))) {
				OutputLastErrorNumAndString(_T(__FUNCTION__), __LINE__);
				throw FALSE;
			}
			SetAndOutputTocCDWText(pDesc,
				pTmpText, wEntrySize, wTocTextEntries, wAllTextSize);
			FreeAndNull(pTmpWText);
		}
	}
	catch (BOOL ret) {
		bRet = ret;
	}
	FreeAndNull(pPTocText);
	FreeAndNull(pTmpText);
	return bRet;
}

BOOL GetConfiguration(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc
) {
	CDB::_GET_CONFIGURATION cdb = { 0 };
	cdb.OperationCode = SCSIOP_GET_CONFIGURATION;
	cdb.RequestType = SCSI_GET_CONFIGURATION_REQUEST_TYPE_CURRENT;
	cdb.StartingFeature[1] = FeatureProfileList;
	WORD wSize = sizeof(GET_CONFIGURATION_HEADER);
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

	_declspec(align(4)) GET_CONFIGURATION_HEADER configHeader = { 0 };
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
		, &configHeader, wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		pDisc->SCSI.wCurrentMedia = ProfileCdrom;
		// not false. because undefined mmc1..
		if (pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX40TS) {
			pDevice->FEATURE.byCanCDText = TRUE;
			pDevice->FEATURE.byC2ErrorData = TRUE;
		}
		OutputDriveNoSupportLogA(GET_CONFIGURATION);
	}
	else {
		pDisc->SCSI.wCurrentMedia =
			MAKEWORD(configHeader.CurrentProfile[1], configHeader.CurrentProfile[0]);
		if (*pExecType != drivespeed) {
			if (pDisc->SCSI.wCurrentMedia == ProfileInvalid) {
				OutputDriveLogA(
					"SCSIOP_GET_CONFIGURATION didn't fail. But it couldn't get CurrentMedia on this drive\n");
				if (*pExecType == dvd) {
					OutputDriveLogA("\tSet CurrentMedia to DVD-ROM\n");
					configHeader.CurrentProfile[1] = ProfileDvdRom;
					pDisc->SCSI.wCurrentMedia = ProfileDvdRom;
				}
				else {
					OutputDriveLogA("\tSet CurrentMedia to CD-ROM\n");
					configHeader.CurrentProfile[1] = ProfileCdrom;
					pDisc->SCSI.wCurrentMedia = ProfileCdrom;
				}
			}
			OutputGetConfigurationHeader(&configHeader);

			DWORD dwAllLen =
				MAKELONG(MAKEWORD(configHeader.DataLength[3], configHeader.DataLength[2]),
					MAKEWORD(configHeader.DataLength[1], configHeader.DataLength[0])) -
				sizeof(configHeader.DataLength) + sizeof(GET_CONFIGURATION_HEADER);
			LPBYTE pPConf = NULL;
			LPBYTE lpConf = NULL;
			if (!GetAlignedCallocatedBuffer(pDevice, &pPConf,
				dwAllLen, &lpConf, _T(__FUNCTION__), __LINE__)) {
				return FALSE;
			}
			REVERSE_BYTES_SHORT(&cdb.AllocationLength, &dwAllLen);

			if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
				, lpConf, dwAllLen, &byScsiStatus, _T(__FUNCTION__), __LINE__)
				|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
				// not false. because undefined mmc1..
				OutputDriveNoSupportLogA(GET_CONFIGURATION);
			}
			else {
				OutputGetConfigurationFeatureNumber(pDevice,
					lpConf + sizeof(GET_CONFIGURATION_HEADER), dwAllLen - sizeof(GET_CONFIGURATION_HEADER));
				if (pDevice->byPlxtrDrive == (BYTE)PLXTR_DRIVE_TYPE::PXW1210A ||
					pDevice->byPlxtrDrive == (BYTE)PLXTR_DRIVE_TYPE::PXW1210S) {
					// Somehow SetDiscSpeed fails in PX-W1210...
					pDevice->FEATURE.bySetCDSpeed = FALSE;
				}
			}
			FreeAndNull(pPConf);
		}
	}
	return TRUE;
}

BOOL ReadDiscInformation(
	PEXT_ARG pExtArg,
	PDEVICE pDevice
) {
	CDB::_READ_DISK_INFORMATION cdb = { 0 };
	cdb.OperationCode = SCSIOP_READ_DISC_INFORMATION;
	cdb.Lun = pDevice->address.Lun;
	WORD wSize = sizeof(DISC_INFORMATION);
	REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

	_declspec(align(4)) DISC_INFORMATION discInformation = { 0 };
	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
		, &discInformation,	wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__) ||
		byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		// not false.
		OutputDriveNoSupportLogA(READ_DISC_INFORMATION);
	}
	else {
		OutputDiscInformation(&discInformation);
	}
	return TRUE;
}

BOOL ModeSense10(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc
) {
//	if (pDevice->FEATURE.byModePage2a) {
		CDB::_MODE_SENSE10 cdb = { 0 };
		cdb.OperationCode = SCSIOP_MODE_SENSE10;
#if (NTDDI_VERSION <= NTDDI_WIN7)
		cdb.LogicalUnitNumber = pDevice->address.Lun;
#endif
		cdb.PageCode = MODE_PAGE_CAPABILITIES;
		cdb.Pc = 2;// MODE_SENSE_CURRENT_VALUES;
		WORD wSize = sizeof(CDVD_CAPABILITIES_PAGE_WITH_HEADER10);
		REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

		_declspec(align(4)) CDVD_CAPABILITIES_PAGE_WITH_HEADER10 modesense = { 0 };
		BYTE byScsiStatus = 0;
		if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB10GENERIC_LENGTH
			, &modesense, wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__) ||
			byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			// not false.
			OutputDriveNoSupportLogA(MODE_SENSE10);
			ModeSense(pExecType, pExtArg, pDevice, pDisc);
		}
		else {
			if (modesense.cdvd.PageCode == MODE_PAGE_CAPABILITIES) {
				WORD rsm = MAKEWORD(modesense.cdvd.ReadSpeedMaximum[1],
					modesense.cdvd.ReadSpeedMaximum[0]);
				INT perKb = 176;
				if (pDisc->SCSI.wCurrentMedia == ProfileDvdRom) {
					perKb = 1385;
				}
				else if (pDisc->SCSI.wCurrentMedia == ProfileBDRom) {
					perKb = 4496;
				}
				if (*pExecType == drivespeed) {
					OutputString("ReadSpeedMaximum: %uKB/sec (%ux)\n", rsm, rsm / perKb);
				}
				else {
					pDevice->wMaxReadSpeed = rsm;
					OutputModeParmeterHeader10(&modesense.header);
					OutputCDVDCapabilitiesPage(&modesense.cdvd, perKb);
				}
			}
			else {
				OutputDriveLogA(
					"SCSIOP_MODE_SENSE10 didn't fail. But it couldn't get PageCode on this drive\n");
				ModeSense(pExecType, pExtArg, pDevice, pDisc);
			}
		}
//	}
//	else {
//		OutputDriveNoSupportLogA(MODE_SENSE10);
//	}
	return TRUE;
}

BOOL ReadBufferCapacity(
	PEXT_ARG pExtArg,
	PDEVICE pDevice
) {
	if (pDevice->FEATURE.byReadBufCapa) {
		CDB::_READ_BUFFER_CAPACITY cdb = { 0 };
		cdb.OperationCode = SCSIOP_READ_BUFFER_CAPACITY;
		WORD wSize = sizeof(READ_BUFFER_CAPACITY_DATA);
		REVERSE_BYTES_SHORT(&cdb.AllocationLength, &wSize);

		_declspec(align(4)) READ_BUFFER_CAPACITY_DATA readBufCapaData = { 0 };
		BYTE byScsiStatus = 0;
		if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB12GENERIC_LENGTH
			, &readBufCapaData,	wSize, &byScsiStatus, _T(__FUNCTION__), __LINE__)
			|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			// not false.
			OutputDriveNoSupportLogA(READ_BUFFER_CAPACITY);
		}
		else {
			OutputReadBufferCapacity(&readBufCapaData);
		}
	}
	return TRUE;
}

BOOL SetDiscSpeed(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	DWORD dwDiscSpeedNum
) {
	if ((*pExecType == cd || *pExecType == gd || *pExecType == audio || *pExecType == data)
		&& pDevice->FEATURE.bySetCDSpeed || *pExecType == dvd) {
		WORD wSpeed = 0;
		// https://msdn.microsoft.com/en-us/library/windows/hardware/ff551368(v=vs.85).aspx
		// https://msdn.microsoft.com/ja-jp/library/ff551396(v=vs.85).aspx
		_declspec(align(4)) CDROM_SET_SPEED setspeed = { CdromSetSpeed, 0, 0, CdromDefaultRotation };
		if ((*pExecType == cd || *pExecType == gd || *pExecType == audio || *pExecType == data) &&
			0 < dwDiscSpeedNum && dwDiscSpeedNum <= CD_DRIVE_MAX_SPEED) {
			// http://senbee.seesaa.net/article/26247159.html
			// 2048 x 75 = 153600 B -> 150 KiB
			// 2352 x 75 = 176400 B -> 172,265625 KiB
			wSpeed = (WORD)(CD_RAW_SECTOR_SIZE * 75 * dwDiscSpeedNum / 1000);
			setspeed.ReadSpeed = wSpeed;
		}
#if 1
		else if (*pExecType == dvd &&
			0 < dwDiscSpeedNum && dwDiscSpeedNum <= DVD_DRIVE_MAX_SPEED) {
			// Read and write speeds for the first DVD drives and players were of
			// 1,385 kB/s (1,353 KiB/s); this speed is usually called "1x".
			// 2048 x 75 x 9 = 1384448 B -> 1352 KiB
			wSpeed = (WORD)(DISC_RAW_READ_SIZE * 676 * dwDiscSpeedNum / 1000);
			setspeed.ReadSpeed = wSpeed;
		}
#endif
		else {
			wSpeed = 0xffff;
			setspeed.ReadSpeed = pDevice->wMaxReadSpeed;
		}
		CDB::_SET_CD_SPEED cdb = { 0 };
		cdb.OperationCode = SCSIOP_SET_CD_SPEED;
		REVERSE_BYTES_SHORT(&cdb.ReadSpeed, &wSpeed);
		// https://msdn.microsoft.com/en-us/library/windows/hardware/ff551370(v=vs.85).aspx
		setspeed.RequestType = CdromSetSpeed;

		BYTE byScsiStatus = 0;
		if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB12GENERIC_LENGTH, &setspeed, 
			sizeof(CDROM_SET_SPEED), &byScsiStatus, _T(__FUNCTION__), __LINE__)
			|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			// Somehow PX-W1210S fails...
			OutputDriveNoSupportLogA(SET_CD_SPEED);
			OutputDriveLogA("Or if you use the SATA/IDE to USB adapter, doesn't support this command\n");
		}
		else {
			OutputSetSpeed(&setspeed);
			OutputString(_T("Set the drive speed: %uKB/sec\n"), setspeed.ReadSpeed);
		}
	}
	return TRUE;
}

// feature PLEXTOR drive
BOOL SetSpeedRead(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	BOOL bState
) {
	// PX-708, PXW4012 or older doesn't support SpeedRead
	if (pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX760A ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX755A ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX716AL ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX716A ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX714A ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PX712A ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PREMIUM2 ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PREMIUM ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PXW5224A ||
		pDevice->byPlxtrDrive == PLXTR_DRIVE_TYPE::PXW4824A
		) {
		CONST WORD size = 8;
		BYTE buf[size] = { 0 };

		CDB::_CDB12 cdb = { 0 };
		cdb.OperationCode = SCSIOP_PLXTR_EXTEND;
		cdb.DisablePageOut = TRUE;
		cdb.LogicalBlock[0] = PLXTR_FLAG_SPEED_READ;
		cdb.LogicalBlock[1] = (BYTE)bState;
		cdb.Reserved2 = 0x08;

		BYTE byScsiStatus = 0;
		if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB12GENERIC_LENGTH
			, buf, sizeof(buf), &byScsiStatus, _T(__FUNCTION__), __LINE__)
			|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
			return FALSE;
		}
#if 0
		for (INT i = 0; i < size; i++) {
			OutputString(_T("%02x "), buf[i]);
		}
		OutputString(_T("\n"));
#endif
	}
	return TRUE;
}

BOOL Reset(
	PEXT_ARG pExtArg,
	PDEVICE pDevice
) {
	CDB::_CDB6GENERIC cdb = { 0 };
	cdb.OperationCode = SCSIOP_PLXTR_RESET;

	BYTE byScsiStatus = 0;
	if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB6GENERIC_LENGTH
		, NULL, 0, &byScsiStatus, _T(__FUNCTION__), __LINE__)
		|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
		return FALSE;
	}
	return TRUE;
}

BOOL ReadEeprom(
	PEXT_ARG pExtArg,
	PDEVICE pDevice
) {
	DWORD tLen = 128;
	BOOL bHigh = FALSE;
	switch (pDevice->byPlxtrDrive) {
	case PLXTR_DRIVE_TYPE::PXW5224A:
	case PLXTR_DRIVE_TYPE::PREMIUM:
	case PLXTR_DRIVE_TYPE::PREMIUM2:
		tLen = 160;
		break;
	case PLXTR_DRIVE_TYPE::PX320A:
	case PLXTR_DRIVE_TYPE::PX704A:
	case PLXTR_DRIVE_TYPE::PX708A:
	case PLXTR_DRIVE_TYPE::PX708A2:
		tLen = 256;
		break;
	case PLXTR_DRIVE_TYPE::PX712A:
		tLen = 512;
		break;
	case PLXTR_DRIVE_TYPE::PX714A:
	case PLXTR_DRIVE_TYPE::PX716A:
	case PLXTR_DRIVE_TYPE::PX716AL:
	case PLXTR_DRIVE_TYPE::PX755A:
	case PLXTR_DRIVE_TYPE::PX760A:
		tLen = 256;
		bHigh = TRUE;
		break;
	}
	DWORD BufLen = tLen;
	LPBYTE pPBuf = NULL;
	LPBYTE pBuf = NULL;
	if (!GetAlignedCallocatedBuffer(pDevice, &pPBuf,
		BufLen, &pBuf, _T(__FUNCTION__), __LINE__)) {
		return FALSE;
	}
	CDB::_CDB12 cdb = { 0 };
	cdb.OperationCode = SCSIOP_PLXTR_READ_EEPROM;
	cdb.RelativeAddress = (BYTE)bHigh;

	BOOL bRet = TRUE;
	BYTE byScsiStatus = 0;
	OutputDriveLogA(OUTPUT_DHYPHEN_PLUS_STR(EEPROM));
	try {
		for (BYTE idx = 0; idx < 4; idx++) {
			REVERSE_BYTES(&cdb.TransferLength, &tLen);
			if (!ScsiPassThroughDirect(pExtArg, pDevice, &cdb, CDB12GENERIC_LENGTH
				, pBuf, BufLen, &byScsiStatus, _T(__FUNCTION__), __LINE__)
				|| byScsiStatus >= SCSISTAT_CHECK_CONDITION) {
				throw FALSE;
			}
			OutputEeprom(pBuf, idx, pDevice->byPlxtrDrive);
			if (bHigh) {
				tLen += 0x10000;
			}
			else {
				break;
			}
		}
	}
	catch (BOOL bErr) {
		bRet = bErr;
	}
	FreeAndNull(pPBuf);
	return bRet;
}

BOOL ReadDriveInformation(
	PEXEC_TYPE pExecType,
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc,
	DWORD dwCDSpeed
) {
	if (*pExecType != drivespeed) {
		BOOL bBusTypeUSB = FALSE;
		if (!StorageQueryProperty(pDevice, &bBusTypeUSB)) {
			return FALSE;
		}
		if (!bBusTypeUSB) {
			if (!ScsiGetAddress(pDevice)) {
				return FALSE;
			}
		}
	}
	// 3rd: get drive vendor, product id here (because use IsValidPlextorDrive)
	if (!Inquiry(pExecType, pExtArg, pDevice)) {
		return FALSE;
	}
	// 4th: check PLEXTOR or not here (because use modesense and from there)
	if (!IsValidPlextorDrive(pDevice)) {
		OutputErrorString(_T("[ERROR] This drive isn't latest firmware. Please update.\n"));
		return FALSE;
	}
	if ((PLXTR_DRIVE_TYPE)pDevice->byPlxtrDrive != PLXTR_DRIVE_TYPE::No) {
		if (*pExecType != drivespeed) {
			if (pExtArg->byPre) {
				SupportIndex0InTrack1(pExtArg, pDevice);
			}
			ReadEeprom(pExtArg, pDevice);
		}
		SetSpeedRead(pExtArg, pDevice, TRUE);
	}
	// 5th: get currentMedia, if use CD-Text, C2 error, modesense, readbuffercapacity, SetDiscSpeed or not here.
	if (!GetConfiguration(pExecType, pExtArg, pDevice, pDisc)) {
		return FALSE;
	}
	if (*pExecType == drivespeed) {
		pDevice->FEATURE.byModePage2a = TRUE;
	}
	ModeSense10(pExecType, pExtArg, pDevice, pDisc);
	if (*pExecType != drivespeed) {
#if 0
		if (*pExecType == dvd) {
			SetStreaming(pDevice, dwCDSpeed);
		}
		else {
#endif
			if (dwCDSpeed != 0) {
				SetDiscSpeed(pExecType, pExtArg, pDevice, dwCDSpeed);
			}
#if 0
		}
#endif
		ReadBufferCapacity(pExtArg, pDevice);
	}
	return TRUE;
}

BOOL ReadGDForTOC(
	PEXT_ARG pExtArg,
	PDEVICE pDevice,
	PDISC pDisc
) {
	CDB::_READ_CD cdb = { 0 };
	SetReadCDCommand(pDevice, &cdb,
		CDFLAG::_READ_CD::CDDA, 2, CDFLAG::_READ_CD::NoC2, CDFLAG::_READ_CD::NoSub);
	BYTE aToc[CD_RAW_SECTOR_SIZE * 4] = { 0 };
	BYTE bufDec[CD_RAW_SECTOR_SIZE * 2] = { 0 };
	INT nOffset = pDisc->MAIN.nAdjustSectorNum - 1;
	if (pDisc->MAIN.nCombinedOffset < 0) {
		nOffset = pDisc->MAIN.nAdjustSectorNum;
	}
	if (!ExecReadGD(pExtArg, pDevice, pDisc, (LPBYTE)&cdb, FIRST_LBA_FOR_GD + nOffset, 2, aToc, bufDec)) {
		return FALSE;
	}

	// http://hwdocs.webs.com/dreamcast
	/*
	0x110 - 0x113: TOC1
	0x114 - 0x116: LBA(little) |
	0x117        : Ctl/Adr     |
	:                        |-> 100 track
	:                        |
	0x294 - 0x296: LBA(little) |
	0x297        : Ctl/Adr     |
	0x298 - 0x299: Zero
	0x29a        : First track |-> alway "3"
	0x29b        : Ctl/Adr     |-> alway "41"
	0x29c - 0x29d: Zero
	0x29e        : Last track
	0x29f        : Ctl/Adr
	0x2a0 - 0x2a2: Max LBA     |-> alway "b4 61 08" (549300)
	0x2a3        : Ctl/Adr     |-> alway "41"
	*/
	OutputCDMain(fileMainInfo, bufDec, FIRST_LBA_FOR_GD + nOffset, CD_RAW_SECTOR_SIZE);
	if (bufDec[0x110] != 'T' || bufDec[0x111] != 'O' ||
		bufDec[0x112] != 'C' || bufDec[0x113] != '1') {
		OutputErrorString(_T("No GD-ROM data\n"));
		return FALSE;
	}
	SetAndOutputTocForGD(pDisc, bufDec);
	return TRUE;
}
