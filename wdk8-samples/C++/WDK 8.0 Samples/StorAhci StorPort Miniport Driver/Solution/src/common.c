/*++

Copyright (C) Microsoft Corporation, 2009

Module Name:

    common.c

Abstract:

    This file contains common ATA related functions.


Notes:

Revision History:

        Michael Xing (xiaoxing),  December 2009 - initial Storport miniport version

--*/

#if _MSC_VER >= 1200
#pragma warning(push)
#endif

#pragma warning(disable:4214) // bit field types other than int
#pragma warning(disable:4201) // nameless struct/union


#include "generic.h"

VOID
ULong2HexString (
    _In_ ULONG  Number,
    _Inout_updates_bytes_ (Length) PUCHAR StringBuffer,
    _In_ ULONG Length
    );

ULONG
SCSItoATA(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*
    Note: If there is a need to send a command to device,
          the translation routine shall set appropriate value to srbExtension->AtaFunction.
            For example: srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
          Not setting this field can cause the Srb being completed earlier than expected.
*/
{
    ULONG   status;

    if (IsAtapiDevice(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        // Atapi command
        status = SrbConvertToATAPICommand(ChannelExtension, Srb);
    } else if (IsAtaDevice(&ChannelExtension->DeviceExtension->DeviceParameters) ||
               (Srb->Cdb[0] == SCSIOP_REPORT_LUNS) ||
               (Srb->Cdb[0] == SCSIOP_INQUIRY) ) {
        // Ata command, or device enumeration commands.
        status = SrbConvertToATACommand(ChannelExtension, Srb);
    } else {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        status = STOR_STATUS_INVALID_DEVICE_REQUEST;
    }

    return status;
}

ULONG
SrbConvertToATAPICommand(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*
    Note: If there is a need to send a command to device,
          the translation routine shall set appropriate value to srbExtension->AtaFunction.
            For example: srbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;
          Not setting this field can cause the Srb being completed earlier than expected.
*/
{
    ULONG status;

    switch (Srb->Cdb[0]) {
    case SCSIOP_MODE_SENSE:

        if (IsTapeDevice(ChannelExtension->DeviceExtension)) {
            // tape drive specific.
            status = AtapiCommonRequest(ChannelExtension, Srb);
        } else {
            // specail process for 6 bytes command, storahci always send down 10 bytes command to device
            status = AtapiModeSenseRequest(ChannelExtension, Srb);
        }
        break;

    case SCSIOP_MODE_SELECT:

        if (IsTapeDevice(ChannelExtension->DeviceExtension)) {
            // tape drive specific.
            status = AtapiCommonRequest(ChannelExtension, Srb);
        } else {
            // specail process for 6 bytes command, storahci always send down 10 bytes command to device
            status = AtapiModeSelectRequest(ChannelExtension, Srb);
        }
        break;

    case SCSIOP_INQUIRY:

        status = AtapiInquiryRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_REPORT_LUNS:
        //use cached info for this command.
        status = AtaReportLunsCommand(ChannelExtension, (PVOID)Srb);
        break;

    case SCSIOP_ATA_PASSTHROUGH16:

        status = AtaPassThroughRequest(ChannelExtension, Srb);
        break;

    default:

        status = AtapiCommonRequest(ChannelExtension, Srb);
        break;
    }

    return status;
}


ULONG
AtapiCommonRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++

Routine Description:

--*/
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    UNREFERENCED_PARAMETER(ChannelExtension);

    srbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;

    if (Srb->SrbFlags & SRB_FLAGS_DATA_IN) {
        srbExtension->Flags |= ATA_FLAGS_DATA_IN;
    }
    if (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) {
        srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
    }

    // set the transfer mode to be used
    if ( DmaSafeAtapiCommand(Srb->Cdb[0]) ) {
        srbExtension->Flags |= ATA_FLAGS_USE_DMA;
    }

    return STOR_STATUS_SUCCESS;
}

VOID
AtapiInquiryCompletion (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    STOR_UNIT_ATTRIBUTES attributes = {0};

    if (Srb->SrbStatus != SRB_STATUS_SUCCESS) {
        return;
    }

    StorPortSetDeviceQueueDepth(ChannelExtension->AdapterExtension,
                                Srb->PathId,
                                Srb->TargetId,
                                Srb->Lun,
                                ChannelExtension->MaxPortQueueDepth);

    if (IsAtapiDevice(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        attributes.DeviceAttentionSupported = ChannelExtension->DeviceExtension[0].IdentifyPacketData->SerialAtaCapabilities.SlimlineDeviceAttention;
        attributes.AsyncNotificationSupported = IsDeviceSupportsAN(ChannelExtension->DeviceExtension->IdentifyPacketData);
        attributes.D3ColdNotSupported = (ChannelExtension->AdapterExtension->StateFlags.SupportsAcpiDSM == 0);

        StorPortSetUnitAttributes(ChannelExtension->AdapterExtension,
                                  (PSTOR_ADDRESS)&ChannelExtension->DeviceExtension[0].DeviceAddress,
                                  attributes);
    }

    return;
}

ULONG
AtapiInquiryRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++

Routine Description:

--*/
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    UNREFERENCED_PARAMETER(ChannelExtension);

    srbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;
    srbExtension->CompletionRoutine = AtapiInquiryCompletion;
    srbExtension->Flags |= ATA_FLAGS_DATA_IN;

    return STOR_STATUS_SUCCESS;
}

VOID
AtapiModeCommandRequestCompletion (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++

Description

    Used as completion routine for: AtapiModeSenseRequest and AtapiModeSelectRequest

Arguments:

Return value:

--*/
{
    ULONG               transferLength = *SrbGetDataTransferLength(Srb);
    UCHAR               bytesAdjust = sizeof(MODE_PARAMETER_HEADER10) - sizeof(MODE_PARAMETER_HEADER);
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    if (Srb->SrbStatus == SRB_STATUS_BUS_RESET) {
        if (srbExtension->DataBuffer != NULL) {
            AhciFreeDmaBuffer(ChannelExtension->AdapterExtension, (ULONG_PTR)srbExtension->CompletionContext, srbExtension->DataBuffer);
            srbExtension->DataBuffer = NULL;
        }
        return;
    }

    if ((Srb->DataBuffer == NULL) || (srbExtension->DataBuffer == NULL) ||
        (Srb->DataTransferLength < sizeof(MODE_PARAMETER_HEADER)) ||
        (srbExtension->DataTransferLength < sizeof(MODE_PARAMETER_HEADER)) ) {
        // free the memory and mark the Srb with error status.
        if (srbExtension->DataBuffer != NULL) {
            AhciFreeDmaBuffer(ChannelExtension->AdapterExtension, (ULONG_PTR)srbExtension->CompletionContext, srbExtension->DataBuffer);
            srbExtension->DataBuffer = NULL;
        }

        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return;
    }

    if (srbExtension->Cdb.CDB10.OperationCode == SCSIOP_MODE_SENSE10) {
        // if it's completion routine for MODE SENSE, copy the data back to original buffer
        PUCHAR originalBuffer;
        PUCHAR currentBuffer;
        PMODE_PARAMETER_HEADER header;
        PMODE_PARAMETER_HEADER10 header10;

        originalBuffer = (PUCHAR)Srb->DataBuffer;
        currentBuffer = (PUCHAR)srbExtension->DataBuffer;

        NT_ASSERT(originalBuffer != NULL);
        NT_ASSERT(currentBuffer != NULL);

        header = (PMODE_PARAMETER_HEADER)originalBuffer;
        header10 = (PMODE_PARAMETER_HEADER10)currentBuffer;

        // Mode parameter header 10 and mode parameter header 6 differ by 3 bytes
        header->ModeDataLength = header10->ModeDataLength[1] - 3;
        header->MediumType = header10->MediumType;

        // ATAPI Mode Parameter Header doesn't have these fields.
        header->DeviceSpecificParameter = header10->DeviceSpecificParameter;

        header->BlockDescriptorLength = header10->BlockDescriptorLength[1];

        // copy the rest of the data
        if (transferLength > sizeof(MODE_PARAMETER_HEADER10)) {
            StorPortCopyMemory(originalBuffer+sizeof(MODE_PARAMETER_HEADER),
                               currentBuffer+sizeof(MODE_PARAMETER_HEADER10),
                               min(Srb->DataTransferLength - sizeof(MODE_PARAMETER_HEADER), transferLength - sizeof(MODE_PARAMETER_HEADER10)));
        }

        // adjust data transfer length.
        if (transferLength > bytesAdjust) {
            Srb->DataTransferLength = transferLength - bytesAdjust;
        } else {
            // error. transfer length should be zero.
            // if it is less than the header, we will just pass it up.
            Srb->DataTransferLength = transferLength;
        }
    }

    if (srbExtension->DataBuffer != NULL) {
        AhciFreeDmaBuffer(ChannelExtension->AdapterExtension, (ULONG_PTR)srbExtension->CompletionContext, srbExtension->DataBuffer);
        srbExtension->DataBuffer = NULL;
    }

    return;
}

ULONG
AtapiModeSenseRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++
--*/
{
    ULONG       status;
    ULONG       modeSenseBufferSize;
    ULONG       tempLength;
    PMODE_PARAMETER_HEADER10    modeSenseBuffer;
    STOR_PHYSICAL_ADDRESS       modeSensePhysialAddress;
    PUCHAR      origBuffer;
    UCHAR       bytesAdjust;

    PCDB                     cdb;
    USHORT                   allocationLength;
    PMODE_PARAMETER_HEADER   header;
    PMODE_PARAMETER_HEADER10 header10;

    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    status = STOR_STATUS_SUCCESS;
    modeSenseBuffer = NULL;
    modeSenseBufferSize = 0;

    bytesAdjust = sizeof(MODE_PARAMETER_HEADER10) - sizeof(MODE_PARAMETER_HEADER);

    // Validate input length
    if (Srb->DataTransferLength < sizeof(MODE_PARAMETER_HEADER)) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_BUFFER_TOO_SMALL;
        goto Done;
    }

    if (Srb->DataBuffer == NULL) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_PARAMETER;
        goto Done;
    }

    modeSenseBufferSize = Srb->DataTransferLength + bytesAdjust;
    origBuffer = (PUCHAR)Srb->DataBuffer;

    if (modeSenseBufferSize < sizeof(MODE_PARAMETER_HEADER10) ) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_BUFFER_TOO_SMALL;
        goto Done;
    }

    // We need to allocate a new data buffer since the size is different
    status = AhciAllocateDmaBuffer(ChannelExtension->AdapterExtension, modeSenseBufferSize, (PVOID*)&modeSenseBuffer);
    if ( (status != STOR_STATUS_SUCCESS) ||
         (modeSenseBuffer == NULL) ) {
        // memory allocation failed
        Srb->SrbStatus = SRB_STATUS_ERROR;
        status = STOR_STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    AhciZeroMemory((PCHAR)modeSenseBuffer, modeSenseBufferSize);
    modeSensePhysialAddress = StorPortGetPhysicalAddress(ChannelExtension->AdapterExtension, NULL, (PVOID)modeSenseBuffer, &tempLength);

    // filll information in the new MODE_SENSE10 header
    allocationLength = ((PCDB)Srb->Cdb)->MODE_SENSE.AllocationLength;

    header = (PMODE_PARAMETER_HEADER)origBuffer;
    header10 = (PMODE_PARAMETER_HEADER10)modeSenseBuffer;

    header10->ModeDataLength[1] = header->ModeDataLength;
    header10->MediumType = header->MediumType;
    header10->BlockDescriptorLength[1] = header->BlockDescriptorLength;

    allocationLength += bytesAdjust;

    // set up cdb of MODE_SENSE10
    cdb = (PCDB)&srbExtension->Cdb;

    cdb->MODE_SENSE10.OperationCode          = SCSIOP_MODE_SENSE10;
    cdb->MODE_SENSE10.Dbd                    = ((PCDB)Srb->Cdb)->MODE_SENSE.Dbd;
    cdb->MODE_SENSE10.PageCode               = ((PCDB)Srb->Cdb)->MODE_SENSE.PageCode;
    cdb->MODE_SENSE10.Pc                     = ((PCDB)Srb->Cdb)->MODE_SENSE.Pc;
    cdb->MODE_SENSE10.AllocationLength[0]    = (UCHAR) (allocationLength >> 8);
    cdb->MODE_SENSE10.AllocationLength[1]    = (UCHAR) (allocationLength & 0xff);
    cdb->MODE_SENSE10.Control                = ((PCDB)Srb->Cdb)->MODE_SENSE.Control;

    // fill in the srbExtension fields
    srbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;
    srbExtension->Flags |= ATA_FLAGS_NEW_CDB;
    srbExtension->Flags |= ATA_FLAGS_DATA_IN;
    srbExtension->DataBuffer = modeSenseBuffer;
    srbExtension->DataTransferLength = modeSenseBufferSize;
    srbExtension->CompletionRoutine = AtapiModeCommandRequestCompletion;
    srbExtension->CompletionContext = (PVOID)modeSenseBufferSize;    // preserve the buffer size, it's needed for freeing the memory

    srbExtension->LocalSgl.NumberOfElements = 1;
    srbExtension->LocalSgl.List[0].PhysicalAddress.LowPart = modeSensePhysialAddress.LowPart;
    srbExtension->LocalSgl.List[0].PhysicalAddress.HighPart = modeSensePhysialAddress.HighPart;
    srbExtension->LocalSgl.List[0].Length = modeSenseBufferSize;
    srbExtension->Sgl = &srbExtension->LocalSgl;

Done:

    if (status != STOR_STATUS_SUCCESS) {
        if (modeSenseBuffer != NULL) {
            AhciFreeDmaBuffer(ChannelExtension->AdapterExtension, modeSenseBufferSize, modeSenseBuffer);
        }
        srbExtension->DataBuffer = NULL;
    }

    return status;
}


ULONG
AtapiModeSelectRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++
    return value: STOR_STATUS
--*/
{
    ULONG   status;
    PVOID   modeSelectBuffer;
    ULONG   modeSelectBufferSize;
    ULONG   tempLength;
    STOR_PHYSICAL_ADDRESS   modeSelectPhysialAddress;
    UCHAR   bytesToSkip;
    PUCHAR  origBuffer;
    UCHAR   bytesAdjust;

    PCDB                     cdb;
    USHORT                   paramListLength;
    PMODE_PARAMETER_HEADER   header;
    PMODE_PARAMETER_HEADER10 header10;

    PAHCI_SRB_EXTENSION      srbExtension = GetSrbExtension(Srb);

    status = STOR_STATUS_SUCCESS;
    modeSelectBuffer = NULL;
    modeSelectBufferSize = 0;

    origBuffer = (PUCHAR)Srb->DataBuffer;
    header = (PMODE_PARAMETER_HEADER)origBuffer;

    if (Srb->DataBuffer == NULL) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_PARAMETER;
        goto Done;
    }

    // Validate input length
    if ( (Srb->DataTransferLength < sizeof(MODE_PARAMETER_HEADER)) ||
         (Srb->DataTransferLength < (sizeof(MODE_PARAMETER_HEADER) + header->BlockDescriptorLength)) ) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_BUFFER_TOO_SMALL;
        goto Done;
    }

    // Check for invalid BlockDescriptorLength
    if ( (sizeof(MODE_PARAMETER_HEADER) + header->BlockDescriptorLength) > 0xFF ) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_PARAMETER;
        goto Done;
    }

    // Atapi devices don't use the block descriptor.
    bytesAdjust = sizeof(MODE_PARAMETER_HEADER10) - sizeof(MODE_PARAMETER_HEADER);
    modeSelectBufferSize = Srb->DataTransferLength + bytesAdjust - header->BlockDescriptorLength;

    // allocate buffer for the new cdb
    status = AhciAllocateDmaBuffer(ChannelExtension->AdapterExtension, modeSelectBufferSize, (PVOID*)&modeSelectBuffer);

    if ( (status != STOR_STATUS_SUCCESS) ||
         (modeSelectBuffer == NULL) ) {
        //
        Srb->SrbStatus = SRB_STATUS_ERROR;
        status = STOR_STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    AhciZeroMemory((PCHAR)modeSelectBuffer, modeSelectBufferSize);
    modeSelectPhysialAddress = StorPortGetPhysicalAddress(ChannelExtension->AdapterExtension, NULL, (PVOID)modeSelectBuffer, &tempLength);

    header10 = (PMODE_PARAMETER_HEADER10)modeSelectBuffer;

    header10->ModeDataLength[1] = header->ModeDataLength;
    header10->MediumType = header->MediumType;

    // block descriptor length in header10 should be 0 for ATAPI devices
    // do not copy the block descriptor. Atapi devices don't use the block descriptor.
    bytesToSkip = sizeof(MODE_PARAMETER_HEADER) + header->BlockDescriptorLength;

    // Copy any remaining buffer contents
    if (Srb->DataTransferLength > bytesToSkip) {

        StorPortCopyMemory( ((PUCHAR)modeSelectBuffer + sizeof(MODE_PARAMETER_HEADER10)),
                            ((PUCHAR)origBuffer + bytesToSkip),
                            (Srb->DataTransferLength - bytesToSkip) );
    }

    paramListLength = ((PCDB)Srb->Cdb)->MODE_SELECT.ParameterListLength;
    paramListLength += sizeof(MODE_PARAMETER_HEADER10);

    // Check for integer overflow
    if (paramListLength < sizeof(MODE_PARAMETER_HEADER10)) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_PARAMETER;
        goto Done;
    }

    // Invalid paramListLength in the original cdb
    if (paramListLength < bytesToSkip) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_PARAMETER;
        goto Done;
    }

    paramListLength -= bytesToSkip;

    // fill in the srbExtension fields
    srbExtension->AtaFunction = ATA_FUNCTION_ATAPI_COMMAND;
    srbExtension->Flags |= ATA_FLAGS_NEW_CDB;
    srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
    srbExtension->DataBuffer = modeSelectBuffer;
    srbExtension->DataTransferLength = modeSelectBufferSize;
    srbExtension->CompletionRoutine = AtapiModeCommandRequestCompletion;
    srbExtension->CompletionContext = (PVOID)modeSelectBufferSize;    // preserve the buffer size, it's needed for freeing the memory

    cdb = (PCDB)&srbExtension->Cdb;

    // fill in the cdb
    cdb->MODE_SELECT10.OperationCode          = SCSIOP_MODE_SELECT10;  // ATAPI device supports MODE SELECT 10
    cdb->MODE_SELECT10.SPBit                  = ((PCDB)Srb->Cdb)->MODE_SELECT.SPBit;
    cdb->MODE_SELECT10.PFBit                  = 1;
    cdb->MODE_SELECT10.LogicalUnitNumber      = ((PCDB)Srb->Cdb)->MODE_SELECT.LogicalUnitNumber;
    cdb->MODE_SELECT10.ParameterListLength[0] = (UCHAR)(paramListLength >> 8);
    cdb->MODE_SELECT10.ParameterListLength[1] = (UCHAR)(paramListLength & 0xff);
    cdb->MODE_SELECT10.Control                = ((PCDB)Srb->Cdb)->MODE_SELECT.Control;

    srbExtension->LocalSgl.NumberOfElements = 1;
    srbExtension->LocalSgl.List[0].PhysicalAddress.LowPart = modeSelectPhysialAddress.LowPart;
    srbExtension->LocalSgl.List[0].PhysicalAddress.HighPart = modeSelectPhysialAddress.HighPart;
    srbExtension->LocalSgl.List[0].Length = modeSelectBufferSize;    // preserve the buffer size, it's needed for freeing the memory
    srbExtension->Sgl = &srbExtension->LocalSgl;

Done:

    if (status != STOR_STATUS_SUCCESS) {
        if (modeSelectBuffer != NULL) {
            AhciFreeDmaBuffer(ChannelExtension->AdapterExtension, modeSelectBufferSize, modeSelectBuffer);
        }
        srbExtension->DataBuffer = NULL;
    }

    return status;
}


ULONG
SrbConvertToATACommand(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*
    Srb to a ATA device must be translated to an ATA command

    Note: If there is a need to send a command to device,
          the translation routine shall set appropriate value to srbExtension->AtaFunction.
            For example: srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
          Not setting this field can cause the Srb being completed earlier than expected.
*/
{
    ULONG status;

    // translate the cdb to task file register values
    switch (Srb->Cdb[0]) {
    case SCSIOP_READ:
    case SCSIOP_WRITE:
    case SCSIOP_READ16:
    case SCSIOP_WRITE16:

        status = AtaReadWriteRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_VERIFY:
    case SCSIOP_VERIFY16:

        status = AtaVerifyRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_MODE_SENSE:
    case SCSIOP_MODE_SENSE10:

        status = AtaModeSenseRequest(ChannelExtension, Srb);

        break;

    case SCSIOP_MODE_SELECT:
    case SCSIOP_MODE_SELECT10:

        status = AtaModeSelectRequest(ChannelExtension, Srb);

        break;

    case SCSIOP_READ_CAPACITY:
    case SCSIOP_READ_CAPACITY16:

        status = AtaReadCapacityRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_INQUIRY:

        status = AtaInquiryRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_REPORT_LUNS:
        status = AtaReportLunsCommand(ChannelExtension, (PVOID)Srb);
        break;

    case SCSIOP_START_STOP_UNIT:

        status = AtaStartStopUnitRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_TEST_UNIT_READY:

        status = AtaTestUnitReadyRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_MEDIUM_REMOVAL:

        status = AtaMediumRemovalRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_SYNCHRONIZE_CACHE:

        status = AtaFlushCommandRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_SECURITY_PROTOCOL_IN:
    case SCSIOP_SECURITY_PROTOCOL_OUT:
        // Use a common function as SECURITY_PROTOCOL_IN and SECURITY_PROTOCOL_OUT structures are same.
        status = AtaSecurityProtocolRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_ATA_PASSTHROUGH16:

        status = AtaPassThroughRequest(ChannelExtension, Srb);
        break;

    case SCSIOP_UNMAP:
        status = AtaUnmapRequest(ChannelExtension, Srb);
        break;

    default:

        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    return status;
}


VOID
AtaSetTaskFileDataRange(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ ULONG64 StartingLba,
    _In_ ULONG NumberOfBlocks
    )
/*++

Routine Description:

    Sets up the following registers in the task file:
    sectorCount
    sectorNum
    CylLow
    CylHigh
    deviceHead


Arguments:

    StartingLba
    NumberOfBlocks

Return Value:

    None.

--*/
{
    LARGE_INTEGER startingSector;
    PATAREGISTERS previousReg;
    PATAREGISTERS currentReg;
    ULONG         sectorCount;

    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    startingSector.QuadPart = StartingLba;
    sectorCount = NumberOfBlocks;

    NT_ASSERT(sectorCount != 0);
    NT_ASSERT(sectorCount <= 0x10000);

    previousReg = &srbExtension->TaskFile.Previous;
    currentReg  = &srbExtension->TaskFile.Current;

    if (Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        // load the higher order bytes - PreviousReg
        SetSectorCount(previousReg, (UCHAR)((sectorCount & 0x0000ff00) >> 8));

        SetSectorNumber(previousReg, (UCHAR)(((startingSector.LowPart) & 0xff000000) >> 24));
        SetCylinderLow(previousReg, (UCHAR)(((startingSector.HighPart) & 0x000000ff) >> 0));
        SetCylinderHigh(previousReg, (UCHAR)(((startingSector.HighPart) & 0x0000ff00) >> 8));

        // setup the device/head register.
        SetDeviceReg(currentReg, IDE_LBA_MODE);
    } else {
        // we cannot handle more than 256 sectors
        NT_ASSERT(sectorCount <= 0x100);

        // The starting LBA should be less than 28 bits wide
        NT_ASSERT(startingSector.HighPart == 0);

        // The upper layers could still send us sectors beyond the
        // end of the disk. Do not assert. Let the device fail it.
        //
        //NT_ASSERT((startingSector.LowPart & 0xF0000000) == 0);

        // setup the device/head register.
        SetDeviceReg(currentReg, (UCHAR)(IDE_LBA_MODE | (((startingSector.LowPart) & 0x0f000000) >> 24)));
    }

    // load the lower order bytes - CurrentReg
    SetSectorCount(currentReg, (UCHAR)((sectorCount & 0x000000ff) >> 0));

    SetSectorNumber(currentReg, (UCHAR)(((startingSector.LowPart) & 0x000000ff) >> 0));
    SetCylinderLow(currentReg, (UCHAR)(((startingSector.LowPart) & 0x0000ff00) >> 8));
    SetCylinderHigh(currentReg, (UCHAR)(((startingSector.LowPart) & 0x00ff0000) >> 16));

    return;
}


VOID
AtaSetWriteCommand (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++

Routine Description:

    Sets the ATA write command in the TaskFile. The routine shall not modify
    any other field in the srb or device extension.

Arguments:

    ChannelExtension
    Srb

Return Value:

    The ATA command that was set in the SRB.

--*/
{
    UCHAR commandReg = IDE_COMMAND_NOT_VALID;

    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    // for SATA devices, use Write DMA command.
    if( ChannelExtension->StateFlags.NCQ_Activated ) {
        // change the command to be NCQ command
        commandReg = IDE_COMMAND_WRITE_FPDMA_QUEUED;
    } else {
    // Make all writes into WRITE_DMA
        // If FUA, keep track of FUA even there is nothing to do with this bit later
        if (Is48BitCommand(srbExtension->Flags)) {
            if ( (((PCDB)Srb->Cdb)->CDB10.ForceUnitAccess) &&
                    IsFuaSupported(ChannelExtension) ) {
                // DMA write ext FUA (48 bit)
                commandReg = IDE_COMMAND_WRITE_DMA_FUA_EXT;
            } else {
                // DMA write ext (48 bit)
                commandReg = IDE_COMMAND_WRITE_DMA_EXT;
            }
        } else {
            // DMA write
            commandReg = IDE_COMMAND_WRITE_DMA;
        }
    }

    SetCommandReg((&srbExtension->TaskFile.Current), commandReg);

    return;
}

VOID
AtaSetReadCommand (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++

Routine Description:


Arguments:

    Srb

Return Value:

    The ATA command that was set in the SRB extension.

--*/
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    UCHAR commandReg = IDE_COMMAND_NOT_VALID;

    UNREFERENCED_PARAMETER(ChannelExtension);

    if( ChannelExtension->StateFlags.NCQ_Activated ) {
        // change the command to be NCQ command
        commandReg = IDE_COMMAND_READ_FPDMA_QUEUED;
    } else {
        // SATA device, use DMA read command.
        if (Is48BitCommand(srbExtension->Flags)) {
            commandReg = IDE_COMMAND_READ_DMA_EXT;
        } else {
            commandReg = IDE_COMMAND_READ_DMA;
        }
    }

    SetCommandReg((&srbExtension->TaskFile.Current), commandReg);

    return;
}

VOID
AtaConstructReadWriteTaskFile (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++

Routine Description:

    Sets up the task file registers for read/write. The routine shall not
    modify any other field in the srb or device extension.

Arguments:

    ChannelExtension
    Srb

Return Value:

    None.

Notes:

    All the other fields in the srb should be set before calling this routine.

--*/
{
    LARGE_INTEGER   startingSector;
    ULONG           bytesPerSector;
    ULONG           sectorCount;

    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    bytesPerSector = BytesPerLogicalSector(&ChannelExtension->DeviceExtension->DeviceParameters);

    // Set up sector count register. Round up to next block.
    sectorCount = (Srb->DataTransferLength + bytesPerSector - 1) / bytesPerSector;

    // Get starting sector number from CDB.
    startingSector.QuadPart = GetLbaFromCdb((PCDB)Srb->Cdb, Srb->CdbLength);

    AtaSetTaskFileDataRange(ChannelExtension,
                            Srb,
                            startingSector.QuadPart,
                            sectorCount
                            );

    if (srbExtension->AtaFunction == ATA_FUNCTION_ATA_READ) {
        AtaSetReadCommand(ChannelExtension, Srb);
    } else {
        AtaSetWriteCommand(ChannelExtension, Srb);
    }

    return;
}

ULONG
AtaReadWriteRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    if (!IsAtaDevice(&ChannelExtension->DeviceExtension->DeviceParameters) ||
        (ChannelExtension->DeviceExtension->DeviceParameters.BytesPerLogicalSector == 0) ) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    //
    // A read write command must have this flag set.
    // The absence of the flag indicates a null MDL. We
    // could get such an SRB with a bad passthrough
    // request. Protect against it. (This might look like
    // a hack, but it is not. We should verify our assumptions
    // before we do a translation)
    //
    if (!(Srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION)) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    // set the transfer mode to be used
    srbExtension->Flags |= ATA_FLAGS_USE_DMA;

    if (Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        srbExtension->Flags |= ATA_FLAGS_48BIT_COMMAND;
    }

    if ( (Srb->Cdb[0] == SCSIOP_READ) || (Srb->Cdb[0] == SCSIOP_READ16) ) {
        srbExtension->AtaFunction = ATA_FUNCTION_ATA_READ;
        srbExtension->Flags |= ATA_FLAGS_DATA_IN;
    } else {
        srbExtension->AtaFunction = ATA_FUNCTION_ATA_WRITE;
        srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
    }

    AtaConstructReadWriteTaskFile(ChannelExtension, Srb);

    return STOR_STATUS_SUCCESS;
}

ULONG
AtaVerifyRequest(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++

Routine Description:

    This routine handles IDE Verify.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    Srb - IO request packet

Return Value:

    SRB status

--*/

{
    LARGE_INTEGER   startingSector;
    ULONG           sectorCount;
    UCHAR           commandReg;

    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    // Get starting sector number from CDB.
    startingSector.QuadPart = GetLbaFromCdb((PCDB)Srb->Cdb, Srb->CdbLength);

    // get the sector count from the cdb
    sectorCount = GetSectorCountFromCdb((PCDB)Srb->Cdb, Srb->CdbLength);

    // Ensure that the command is small enough for us to handle
    if (sectorCount > 0x10000) {
        // Sector count cannot be longer than 2 bytes (2 ^ 16).
        // Note that a 0 sector count => 0x10000 sectors
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    if (!Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters) && (sectorCount > 0x100)) {
        // Without 48bit support the sector count cannot be greater than 256 sectors
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;

    if (Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        srbExtension->Flags |= ATA_FLAGS_48BIT_COMMAND;
    }

    AtaSetTaskFileDataRange(ChannelExtension,
                            Srb,
                            startingSector.QuadPart,
                            sectorCount
                            );

    // verify command. no data transfer
    if (Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        commandReg = IDE_COMMAND_VERIFY_EXT;
    } else {
        commandReg = IDE_COMMAND_VERIFY;
    }

    SetCommandReg((&srbExtension->TaskFile.Current), commandReg);

    return STOR_STATUS_SUCCESS;

}

VOID
AtaModeSenseRequestCompletionMediaStatus (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION    srbExtension = GetSrbExtension(Srb);
    PMODE_PARAMETER_HEADER modePageHeader = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;

    UNREFERENCED_PARAMETER(ChannelExtension);

    if (Srb->SrbStatus == SRB_STATUS_BUS_RESET) {
        return;
    }

    if ( Srb->DataTransferLength < sizeof(PMODE_PARAMETER_HEADER) ) {
        NT_ASSERT(FALSE);
        return;
    }
    // update the mode page header
    NT_ASSERT(modePageHeader != NULL);

    if ( (srbExtension->AtaStatus & IDE_STATUS_ERROR) &&
         (srbExtension->AtaError & IDE_ERROR_DATA_ERROR) ) {
        modePageHeader->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
    }

    return;
}

VOID
AtaModeSenseRequestCompletionWriteCache (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PMODE_CACHING_PAGE      cachePage;

    PAHCI_SRB_EXTENSION     srbExtension = GetSrbExtension(Srb);
    PMODE_PARAMETER_HEADER  modePageHeader = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;
    PIDENTIFY_DEVICE_DATA   identifyDeviceData = (PIDENTIFY_DEVICE_DATA)srbExtension->DataBuffer;

    //Initialize variables
    NT_ASSERT(identifyDeviceData != NULL);
    NT_ASSERT(modePageHeader != NULL);
    cachePage = (PMODE_CACHING_PAGE)(modePageHeader + 1);

    if (Srb->SrbStatus == SRB_STATUS_BUS_RESET) {
        return;
    }

    // update the mode page header
    if ( (Srb->DataBuffer == NULL) ||
         (Srb->DataTransferLength < ( sizeof(PMODE_PARAMETER_HEADER) + sizeof(PMODE_CACHING_PAGE) ) ) ) {
        Srb->SrbStatus = SRB_STATUS_ERROR;
        return;
    }

    if (Srb->SrbStatus == SRB_STATUS_SUCCESS) {
        cachePage->WriteCacheEnable = (UCHAR)identifyDeviceData->CommandSetActive.WriteCache;
    } else {
        Srb->SrbStatus = SRB_STATUS_ERROR;
    }

    // update the number of bytes we are returning.  If this function executes, the maximum size is being used.
    Srb->DataTransferLength = Srb->DataTransferLength - sizeof(MODE_PARAMETER_HEADER) - sizeof(MODE_CACHING_PAGE);

    if (IsRemovableMedia(&ChannelExtension->DeviceExtension->DeviceParameters) &&
        IsMsnEnabled(ChannelExtension->DeviceExtension)) {
        // prepare to send no data command. reuse srbExtension area, clear it first
        AhciZeroMemory((PCHAR)srbExtension, sizeof(AHCI_SRB_EXTENSION));

        srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
        SetCommandReg((&srbExtension->TaskFile.Current), IDE_COMMAND_GET_MEDIA_STATUS);

        return;
    }

    return;
}


ULONG
AtaModeSenseRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*
  NOTE: only Cache Page is supported by storahci driver
*/
{
    ULONG bytesLeft;
    PCDB  cdb;
    PMODE_PARAMETER_HEADER modePageHeader;

    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    cdb = (PCDB)Srb->Cdb;

    // only support page control for current values
    if (cdb->MODE_SENSE.Pc != 0) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    // make sure the output buffer is at least the size of the header
    if (Srb->DataTransferLength < sizeof(MODE_PARAMETER_HEADER)) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_BUFFER_TOO_SMALL;
    }

    modePageHeader = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;

    if (modePageHeader == NULL) {
        Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        //Srb->InternalStatus = STATUS_INSUFFICIENT_RESOURCES;
        return STOR_STATUS_INSUFFICIENT_RESOURCES;
    }

    // initialize to success
    Srb->SrbStatus = SRB_STATUS_SUCCESS;

    // first update the mode page header
    NT_ASSERT(modePageHeader != NULL);

    AhciZeroMemory((PCHAR)modePageHeader, Srb->DataTransferLength);

    modePageHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER) - FIELD_OFFSET(MODE_PARAMETER_HEADER, MediumType);

    // now service the cache page
    bytesLeft = Srb->DataTransferLength - sizeof(MODE_PARAMETER_HEADER);

    if ((cdb->MODE_SENSE.PageCode == MODE_SENSE_RETURN_ALL) || (cdb->MODE_SENSE.PageCode == MODE_PAGE_CACHING)) {
        if (bytesLeft >= sizeof(MODE_CACHING_PAGE)) {
            // cache settings page
            PMODE_CACHING_PAGE cachePage;

            cachePage = (PMODE_CACHING_PAGE)(modePageHeader + 1);
            cachePage->PageCode = MODE_PAGE_CACHING;
            cachePage->PageSavable = 0;
            cachePage->PageLength = 0xa;
            cachePage->ReadDisableCache = 0;
            cachePage->WriteCacheEnable = 0;  //initialized value, likely to change below

            if (IsFuaSupported(ChannelExtension)) {
                modePageHeader->DeviceSpecificParameter &= MODE_DSP_FUA_SUPPORTED;
            }
            bytesLeft -= sizeof(MODE_CACHING_PAGE);
            modePageHeader->ModeDataLength += sizeof(MODE_CACHING_PAGE);

          //Check to see if the Write Cache is enabled on this device, by issuing an IDENTIFY DEVICE command
            if (ChannelExtension->DeviceExtension->IdentifyDeviceData->CommandSetSupport.WriteCache != 0) {
                srbExtension->AtaFunction = ATA_FUNCTION_ATA_IDENTIFY;
                srbExtension->Flags |= ATA_FLAGS_DATA_IN;
                srbExtension->DataBuffer = ChannelExtension->DeviceExtension->IdentifyDeviceData;
                srbExtension->DataTransferLength = sizeof(IDENTIFY_DEVICE_DATA);
                srbExtension->LocalSgl.NumberOfElements = 1;
                srbExtension->LocalSgl.List[0].PhysicalAddress.LowPart = ChannelExtension->DeviceExtension->IdentifyDataPhysicalAddress.LowPart;
                srbExtension->LocalSgl.List[0].PhysicalAddress.HighPart = ChannelExtension->DeviceExtension->IdentifyDataPhysicalAddress.HighPart;
                srbExtension->LocalSgl.List[0].Length = sizeof(IDENTIFY_DEVICE_DATA);
                srbExtension->Sgl = &srbExtension->LocalSgl;
                srbExtension->CompletionRoutine = AtaModeSenseRequestCompletionWriteCache;

                SetCommandReg((&srbExtension->TaskFile.Current), IDE_COMMAND_IDENTIFY);

                return STOR_STATUS_SUCCESS; //Get Media Protect Status will be preformed in AtaModeSenseRequestCompletionWriteCache.
           }
        } else {
            Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
        }
    }

    // update the number of bytes we are returning
    Srb->DataTransferLength -= bytesLeft;

    if (IsRemovableMedia(&ChannelExtension->DeviceExtension->DeviceParameters) &&
        IsMsnEnabled(ChannelExtension->DeviceExtension)) {
        // this command does not transfer data
        srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
        srbExtension->CompletionRoutine = AtaModeSenseRequestCompletionMediaStatus;

        SetCommandReg((&srbExtension->TaskFile.Current), IDE_COMMAND_GET_MEDIA_STATUS);
    }

    return STOR_STATUS_SUCCESS;
}

ULONG
AtaModeSelectRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*
    NOTE: this is used to enable/disable device write cache.
*/
{
    ULONG                   status = STOR_STATUS_SUCCESS;
    PMODE_PARAMETER_HEADER  modePageHeader;
    PMODE_CACHING_PAGE      cachePage;
    ULONG                   pageOffset;
    ULONG                   bytesLeft;

    PCDB                    cdb = (PCDB)Srb->Cdb;
    PAHCI_SRB_EXTENSION     srbExtension = GetSrbExtension(Srb);

    NT_ASSERT( (cdb->MODE_SELECT.OperationCode == SCSIOP_MODE_SELECT) ||
               (cdb->MODE_SELECT.OperationCode == SCSIOP_MODE_SELECT10) );

    // only support scsi-2 mode select format
    if (cdb->MODE_SELECT.PFBit != 1) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_DEVICE_REQUEST;
    }

    if (Srb->DataTransferLength < sizeof(MODE_PARAMETER_HEADER)) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_DEVICE_REQUEST;
    }

    modePageHeader = Srb->DataBuffer;

    if (modePageHeader == NULL) {
        Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        //Srb->InternalStatus = STATUS_INSUFFICIENT_RESOURCES;
        return STOR_STATUS_INSUFFICIENT_RESOURCES;
    }

    pageOffset = sizeof(MODE_PARAMETER_HEADER) + modePageHeader->BlockDescriptorLength;

    if (Srb->DataTransferLength > pageOffset) {
        bytesLeft = Srb->DataTransferLength - pageOffset;
    } else {
        bytesLeft = 0;
    }

    cachePage = (PMODE_CACHING_PAGE)(((PUCHAR) modePageHeader) + pageOffset);

    if ( (bytesLeft >= sizeof(MODE_CACHING_PAGE)) &&
         (cachePage->PageCode == MODE_PAGE_CACHING) &&
         (cachePage->PageLength == 0xa) ) {

        //if Write Cache is supported
         if (ChannelExtension->DeviceExtension->IdentifyDeviceData->CommandSetSupport.WriteCache != 0) {
            srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;

            if (cachePage->WriteCacheEnable != 0) {
                SetFeaturesReg ((&srbExtension->TaskFile.Current), IDE_FEATURE_ENABLE_WRITE_CACHE);
            } else {
                SetFeaturesReg ((&srbExtension->TaskFile.Current), IDE_FEATURE_DISABLE_WRITE_CACHE);
            }

            SetCommandReg ((&srbExtension->TaskFile.Current), IDE_COMMAND_SET_FEATURE);
        } else {
            // we are in the same cache state
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            status = STOR_STATUS_SUCCESS;
        }

    } else {
        // the request is not for the mode cache page
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_PARAMETER;
    }

    return status;
}


VOID
SelectDeviceGeometry(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PATA_DEVICE_PARAMETERS DeviceParameters,
    _In_ PIDENTIFY_DEVICE_DATA  IdentifyDeviceData
    )
{
    LARGE_INTEGER   maxLba;

    UNREFERENCED_PARAMETER(ChannelExtension);

  // ignore non ata devices
    if (!IsAtaDevice(DeviceParameters)) {
        return;
    }

    maxLba.QuadPart = 0;

    if (Support48Bit(DeviceParameters)) {
        maxLba.LowPart = IdentifyDeviceData->Max48BitLBA[0];
        maxLba.HighPart = IdentifyDeviceData->Max48BitLBA[1];
    } else {
        maxLba.LowPart = IdentifyDeviceData->UserAddressableSectors;
    }

    DeviceParameters->MaxLba = maxLba;

    // assume 512 bytes/sector
    if( IdentifyDeviceData->PhysicalLogicalSectorSize.LogicalSectorLongerThan256Words ) {
        DeviceParameters->BytesPerLogicalSector = IdentifyDeviceData->WordsPerLogicalSector[0] +
                                                 (IdentifyDeviceData->WordsPerLogicalSector[1] << 16);
        DeviceParameters->BytesPerLogicalSector <<= 1;
    }

    if (DeviceParameters->BytesPerLogicalSector < ATA_BLOCK_SIZE) {
        DeviceParameters->BytesPerLogicalSector = ATA_BLOCK_SIZE;
    }

    if( IdentifyDeviceData->PhysicalLogicalSectorSize.MultipleLogicalSectorsPerPhysicalSector ){
        DeviceParameters->BytesPerPhysicalSector = (1 << IdentifyDeviceData->PhysicalLogicalSectorSize.LogicalSectorsPerPhysicalSector) *
                                                   DeviceParameters->BytesPerLogicalSector;
    }

    if (DeviceParameters->BytesPerPhysicalSector < DeviceParameters->BytesPerLogicalSector) {
        DeviceParameters->BytesPerPhysicalSector = DeviceParameters->BytesPerLogicalSector;
    }

    if( IdentifyDeviceData->BlockAlignment.Word209Supported) {
        DeviceParameters->BytesOffsetForSectorAlignment = IdentifyDeviceData->BlockAlignment.AlignmentOfLogicalWithinPhysical *
                                                          DeviceParameters->BytesPerLogicalSector;
    }

    return;
}

ULONG
AtaReadCapacityCompletion (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    ULONG   status;
    ULONG   bytesPerSector;
    ULONG64 maxLba;

    NT_ASSERT(!IsAtapiDevice(&ChannelExtension->DeviceExtension->DeviceParameters));

    maxLba = MaxUserAddressableLba(&ChannelExtension->DeviceExtension->DeviceParameters) - 1;

    bytesPerSector = BytesPerLogicalSector(&ChannelExtension->DeviceExtension->DeviceParameters);

    // fill in read capacity

    if (Srb->DataBuffer != NULL) {

        if (Srb->CdbLength == 0x10) {
            // 16 byte CDB
            PREAD_CAPACITY16_DATA readCap16 = (PREAD_CAPACITY16_DATA)Srb->DataBuffer;
            ULONG                 returnDataLength = 12;    //default to sizeof(READ_CAPACITY_DATA_EX)

            if (Srb->DataTransferLength < sizeof(READ_CAPACITY_DATA_EX)) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                return STOR_STATUS_BUFFER_TOO_SMALL;
            }
            NT_ASSERT(Srb->Cdb[0] == SCSIOP_READ_CAPACITY16);

            REVERSE_BYTES_QUAD(&readCap16->LogicalBlockAddress.QuadPart, &maxLba);
            REVERSE_BYTES(&readCap16->BytesPerBlock, &bytesPerSector);

            if (Srb->DataTransferLength >= FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3)) {
                // buffer is big enough for sector alignment info
                readCap16->ProtectionEnable = 0;    // not support Portection Types.
                readCap16->ProtectionType = 0;
                readCap16->LogicalPerPhysicalExponent = (UCHAR)ChannelExtension->DeviceExtension->IdentifyDeviceData->PhysicalLogicalSectorSize.LogicalSectorsPerPhysicalSector;
                if(ChannelExtension->DeviceExtension->IdentifyDeviceData->BlockAlignment.Word209Supported) {
                    USHORT logicalBlocksPerPhysicalBlock = (USHORT)(1 << readCap16->LogicalPerPhysicalExponent);
                    USHORT lowestAlignedBlock = (logicalBlocksPerPhysicalBlock - ChannelExtension->DeviceExtension->IdentifyDeviceData->BlockAlignment.AlignmentOfLogicalWithinPhysical) %
                                                logicalBlocksPerPhysicalBlock;

                    readCap16->LowestAlignedBlock_MSB = (UCHAR)((lowestAlignedBlock >> 8) & 0x00FF);
                    readCap16->LowestAlignedBlock_LSB = (UCHAR)(lowestAlignedBlock & 0x00FF);
                } else {
                    readCap16->LowestAlignedBlock_LSB = 0;
                    readCap16->LowestAlignedBlock_MSB = 0;
                }
                returnDataLength = FIELD_OFFSET(READ_CAPACITY16_DATA, Reserved3);
            }

            Srb->DataTransferLength = returnDataLength;
        } else {
            // 12 byte CDB
            PREAD_CAPACITY_DATA readCap = (PREAD_CAPACITY_DATA)Srb->DataBuffer;

            NT_ASSERT(Srb->Cdb[0] == SCSIOP_READ_CAPACITY);

            if (Srb->DataTransferLength < sizeof(READ_CAPACITY_DATA)) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                return STOR_STATUS_BUFFER_TOO_SMALL;
            }

            REVERSE_BYTES(&readCap->BytesPerBlock, &bytesPerSector);

            if (maxLba >= MAXULONG) {
                readCap->LogicalBlockAddress = MAXULONG;
            } else {
                ULONG tmp = (ULONG)(maxLba & MAXULONG);
                REVERSE_BYTES(&readCap->LogicalBlockAddress, &tmp);
            }

            Srb->DataTransferLength = sizeof(READ_CAPACITY_DATA);
        }

        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        status = STOR_STATUS_SUCCESS;

    } else {
        Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        status = STOR_STATUS_INSUFFICIENT_RESOURCES;
    }

    return status;
}


VOID
AtaReadCapacityRequestCompletion (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    ULONG               status = STOR_STATUS_SUCCESS;
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);
    PUCHAR              identifyDeviceData = (PUCHAR)srbExtension->DataBuffer;

    NT_ASSERT(IsRemovableMedia(&ChannelExtension->DeviceExtension->DeviceParameters));

    if (Srb->SrbStatus == SRB_STATUS_SUCCESS) {

        ATA_DEVICE_PARAMETERS deviceParameters;

        // make a local copy
        StorPortCopyMemory(&deviceParameters, &ChannelExtension->DeviceExtension->DeviceParameters, sizeof(ATA_DEVICE_PARAMETERS));

        // modify the fields related to media geometry
        SelectDeviceGeometry(ChannelExtension, &deviceParameters, (PIDENTIFY_DEVICE_DATA)identifyDeviceData);

        if (deviceParameters.MaxLba.QuadPart > 1) {
            // the device returned some geometry
            // update the one in the device extension
            StorPortCopyMemory(&ChannelExtension->DeviceExtension->DeviceParameters, &deviceParameters, sizeof(ATA_DEVICE_PARAMETERS));
            status = AtaReadCapacityCompletion(ChannelExtension, Srb);
        } else {
            SENSE_DATA senseBuffer;

            // no media in device
            senseBuffer.ErrorCode = 0x70;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey =  SCSI_SENSE_NOT_READY;
            senseBuffer.AdditionalSenseCode = SCSI_ADSENSE_NO_MEDIA_IN_DEVICE;
            senseBuffer.AdditionalSenseCodeQualifier = 0;

            Srb->DataTransferLength = 0;
            Srb->SrbStatus = SRB_STATUS_ERROR;
            //Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

            // update the sense buffer
            if (Srb->SenseInfoBuffer) {
                UCHAR senseLen = min(Srb->SenseInfoBufferLength, sizeof(SENSE_DATA));

                if (senseLen > 0) {
                    StorPortCopyMemory(Srb->SenseInfoBuffer, &senseBuffer, senseLen);
                    Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                }
            }

            status = STOR_STATUS_UNSUCCESSFUL;
        }
    } else {
        Srb->DataTransferLength = 0;
        if (Srb->SrbStatus != SRB_STATUS_BUS_RESET) {
            Srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
        }
        status = STOR_STATUS_UNSUCCESSFUL;
    }

    UNREFERENCED_PARAMETER(status);

    return;
}

ULONG
AtaReadCapacityRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    ULONG               status = STOR_STATUS_SUCCESS;
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    NT_ASSERT(!IsAtapiDevice(&ChannelExtension->DeviceExtension->DeviceParameters));

    // Verify the data transfer length
    if (Srb->DataTransferLength < sizeof(READ_CAPACITY_DATA)) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_BUFFER_TOO_SMALL;
    }

    if ((Srb->CdbLength == 0x10) &&
        (Srb->DataTransferLength < 12) ) {
        // classpnp may send down this request with buffer size = sizeof(READ_CAPACITY_DATA_EX)
        NT_ASSERT(Srb->Cdb[0] == SCSIOP_READ_CAPACITY16);
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_BUFFER_TOO_SMALL;
    }

    if (IsRemovableMedia(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        // get identify data from the device as the media might be changed.
        srbExtension->AtaFunction = ATA_FUNCTION_ATA_IDENTIFY;
        srbExtension->Flags |= ATA_FLAGS_DATA_IN;
        srbExtension->DataBuffer = ChannelExtension->DeviceExtension->IdentifyDeviceData;
        srbExtension->DataTransferLength = sizeof(IDENTIFY_DEVICE_DATA);
        srbExtension->CompletionRoutine = AtaReadCapacityRequestCompletion;
        srbExtension->LocalSgl.NumberOfElements = 1;
        srbExtension->LocalSgl.List[0].PhysicalAddress.LowPart = ChannelExtension->DeviceExtension->IdentifyDataPhysicalAddress.LowPart;
        srbExtension->LocalSgl.List[0].PhysicalAddress.HighPart = ChannelExtension->DeviceExtension->IdentifyDataPhysicalAddress.HighPart;
        srbExtension->LocalSgl.List[0].Length = sizeof(IDENTIFY_DEVICE_DATA);
        srbExtension->Sgl = &srbExtension->LocalSgl;

        SetCommandReg((&srbExtension->TaskFile.Current), IDE_COMMAND_IDENTIFY);

    } else {
        // fixed media
        status = AtaReadCapacityCompletion(ChannelExtension, Srb);
    }

    return status;
}

VOID
AtaGenerateInquiryData (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_reads_bytes_(ATA_INQUIRYDATA_SIZE) PINQUIRYDATA InquiryData
    )
{
    USHORT descriptor = VER_DESCRIPTOR_1667_NOVERSION;
    ULONG len;
    ULONG vendorIdLength;
    ULONG prodLen;

    AhciZeroMemory((PCHAR)InquiryData, ATA_INQUIRYDATA_SIZE);

    InquiryData->DeviceType = ChannelExtension->DeviceExtension->DeviceParameters.ScsiDeviceType; //DIRECT_ACCESS_DEVICE;
    InquiryData->RemovableMedia = IsRemovableMedia(&ChannelExtension->DeviceExtension->DeviceParameters) ? 1 : 0;
    InquiryData->ResponseDataFormat = 0x2; //data format is defined in SPC standard
    InquiryData->CommandQueue = 1;  //support NCQ for AHCI controller
    InquiryData->AdditionalLength = ATA_INQUIRYDATA_SIZE - RTL_SIZEOF_THROUGH_FIELD(INQUIRYDATA, AdditionalLength);

    // if there is blank space in first 8 chars, use the part before blank space as VendorId
    for (vendorIdLength = 8; vendorIdLength > 0; vendorIdLength--) {
        if (ChannelExtension->DeviceExtension->DeviceParameters.VendorId[vendorIdLength - 1] == ' ') {
            break;
        }
    }

    len = min(vendorIdLength, sizeof(ChannelExtension->DeviceExtension->DeviceParameters.VendorId));

    AhciFillMemory((PCHAR)InquiryData->VendorId, 8, ' ');

    // if there is no blank space in first 8 chars, leave blank spaces in VendorId. Otherwise, copy the string
    if (len > 0 && len < 9) {
        StorPortCopyMemory(InquiryData->VendorId,
                           ChannelExtension->DeviceExtension->DeviceParameters.VendorId,
                           len
                           );
    }

    prodLen = min(16, sizeof(ChannelExtension->DeviceExtension->DeviceParameters.VendorId) - len);

    AhciFillMemory((PCHAR)InquiryData->ProductId, 16, ' ');
    StorPortCopyMemory(InquiryData->ProductId,
                       (ChannelExtension->DeviceExtension->DeviceParameters.VendorId + len),
                       prodLen
                       );

    len = min(4, sizeof(ChannelExtension->DeviceExtension->DeviceParameters.RevisionId));

    AhciFillMemory((PCHAR)InquiryData->ProductRevisionLevel, 4, ' ');
    StorPortCopyMemory(InquiryData->ProductRevisionLevel,
                       ChannelExtension->DeviceExtension->DeviceParameters.RevisionId,
                       len
                       );

    if ( (ChannelExtension->DeviceExtension->IdentifyDeviceData->TrustedComputing.FeatureSupported == 1) &&
         (ChannelExtension->DeviceExtension->IdentifyDeviceData->AdditionalSupported.IEEE1667 == 1) ) {
        // fill in 1667 descriptor
        REVERSE_BYTES_SHORT(&InquiryData->VersionDescriptors[0], &descriptor);
    }

    return;
}

VOID
IssueIdentifyCommand(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++
    This could be a macro.  broken out here to make the logic easier to read
It assumes:
    nothing
Called by:
    AtaInquiryRequest
    AtaReportLunsCommand


It performs:
    1 Fills in the local SRB with the SetFeatures command
    2 Starts processing the command
Affected Variables/Registers:
    none

--*/
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

  //1 Fills in the local SRB
    srbExtension->AtaFunction = ATA_FUNCTION_ATA_IDENTIFY;
    srbExtension->Flags |= ATA_FLAGS_DATA_IN;
    srbExtension->CompletionRoutine = AhciPortIdentifyDevice;

    //setup TaskFile
    srbExtension->TaskFile.Current.bFeaturesReg = 0;
    srbExtension->TaskFile.Current.bSectorCountReg = 0;
    srbExtension->TaskFile.Current.bSectorNumberReg = 0;
    srbExtension->TaskFile.Current.bCylLowReg = 0;
    srbExtension->TaskFile.Current.bCylHighReg = 0;
    srbExtension->TaskFile.Current.bDriveHeadReg = 0xA0;
    srbExtension->TaskFile.Current.bCommandReg = IDE_COMMAND_NOT_VALID; //the command will be set when we can read PxSIG register.
    srbExtension->TaskFile.Current.bReserved = 0;
    srbExtension->TaskFile.Previous.bFeaturesReg = 0;
    srbExtension->TaskFile.Previous.bSectorCountReg = 0;
    srbExtension->TaskFile.Previous.bSectorNumberReg = 0;
    srbExtension->TaskFile.Previous.bCylLowReg = 0;
    srbExtension->TaskFile.Previous.bCylHighReg = 0;
    srbExtension->TaskFile.Previous.bDriveHeadReg = 0;
    srbExtension->TaskFile.Previous.bCommandReg = 0;
    srbExtension->TaskFile.Previous.bReserved = 0;

    Srb->SrbStatus = SRB_STATUS_PENDING;
    srbExtension->DataBuffer = (PVOID)ChannelExtension->DeviceExtension->IdentifyDeviceData;

    srbExtension->LocalSgl.NumberOfElements = 1;
    srbExtension->LocalSgl.List[0].PhysicalAddress.LowPart = ChannelExtension->DeviceExtension->IdentifyDataPhysicalAddress.LowPart;
    srbExtension->LocalSgl.List[0].PhysicalAddress.HighPart = ChannelExtension->DeviceExtension->IdentifyDataPhysicalAddress.HighPart;
    srbExtension->LocalSgl.List[0].Length = sizeof(IDENTIFY_DEVICE_DATA);
    srbExtension->Sgl = &srbExtension->LocalSgl;
    srbExtension->DataTransferLength = sizeof(IDENTIFY_DEVICE_DATA);
    return;
}

ULONG
InquiryComplete(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*++
    This is the completion point of INQUIRY command for ATA devices.

Arguments:

    ChannelExtension
    Srb

Return Value:
    ULONG - status
--*/
{
    ULONG   status = STOR_STATUS_SUCCESS;
    ULONG   dataTransferLength = 0;
    // this is a standard INQUIRY
    UCHAR   inquiryData[ATA_INQUIRYDATA_SIZE] = {0};

    STOR_UNIT_ATTRIBUTES attributes = {0};

    // report error back so that Storport may retry the command.
    if ( (Srb->SrbStatus != SRB_STATUS_PENDING) &&
         (Srb->SrbStatus != SRB_STATUS_SUCCESS) &&
         (Srb->SrbStatus != SRB_STATUS_NO_DEVICE) ) {
        return STOR_STATUS_UNSUCCESSFUL;
    }

    // report error if no device connected
    if ( ChannelExtension->DeviceExtension->DeviceParameters.AtaDeviceType == DeviceNotExist ) {
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        return STOR_STATUS_UNSUCCESSFUL;
    }

    // Indicate the existence of a device
    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    status = STOR_STATUS_SUCCESS;

    // make up inquiry data for ata devices
    AtaGenerateInquiryData(ChannelExtension, (PINQUIRYDATA)inquiryData);

    dataTransferLength = min(Srb->DataTransferLength, ATA_INQUIRYDATA_SIZE);

    if (dataTransferLength > 0) {
        if (Srb->DataBuffer != NULL) {
            StorPortCopyMemory(Srb->DataBuffer, inquiryData, dataTransferLength);
            Srb->DataTransferLength = dataTransferLength;
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            status = STOR_STATUS_SUCCESS;
        } else {
            Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
            //Srb->InternalStatus = STATUS_INSUFFICIENT_RESOURCES;
            status = STOR_STATUS_INSUFFICIENT_RESOURCES;
        }
    } else {
        // this will be a programming error.
        Srb->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        status = STOR_STATUS_INVALID_PARAMETER;
    }

    StorPortSetDeviceQueueDepth(ChannelExtension->AdapterExtension,
                                Srb->PathId,
                                Srb->TargetId,
                                Srb->Lun,
                                ChannelExtension->MaxPortQueueDepth);

    // tell port driver if D3Cold is supported or not for this device
    attributes.D3ColdNotSupported = (ChannelExtension->AdapterExtension->StateFlags.SupportsAcpiDSM == 0);

    StorPortSetUnitAttributes(ChannelExtension->AdapterExtension,
                              (PSTOR_ADDRESS)&ChannelExtension->DeviceExtension[0].DeviceAddress,
                              attributes);

    return status;
}

ULONG
AtaInquiryRequest(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*
  NOTE: the command should be completed after calling this function as no real command will be sent to device.
*/
{
    ULONG status = STOR_STATUS_SUCCESS;
    PCDB  cdb = (PCDB)Srb->Cdb;

    NT_ASSERT(!IsAtapiDevice(&ChannelExtension->DeviceExtension->DeviceParameters));

    // Filter out all TIDs but 0 and 1 since this is an IDE interface which support up to two devices.
    if (Srb->Lun != 0) {
        // Indicate no device found at this address.
        Srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
        status = STOR_STATUS_INVALID_PARAMETER;
    } else if (cdb->CDB6INQUIRY3.EnableVitalProductData == 0) {

        if (IsDumpMode(ChannelExtension->AdapterExtension) && !DeviceIdentificationComplete(ChannelExtension->AdapterExtension)) {
            // the enumeration command from dump stack.
            IssueIdentifyCommand(ChannelExtension, Srb);
        } else {
            status = InquiryComplete(ChannelExtension, Srb);
        }
    } else {
        // the INQUIRY is for VPD page
        AhciZeroMemory((PCHAR)Srb->DataBuffer, Srb->DataTransferLength);

        switch(cdb->CDB6INQUIRY3.PageCode) {
        case VPD_SUPPORTED_PAGES: {
            if (Srb->DataTransferLength < 0x0a) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                status = STOR_STATUS_INVALID_PARAMETER;
            } else {
                PVPD_SUPPORTED_PAGES_PAGE outputBuffer = (PVPD_SUPPORTED_PAGES_PAGE)Srb->DataBuffer;

                outputBuffer->DeviceType = ChannelExtension->DeviceExtension->DeviceParameters.ScsiDeviceType; //DIRECT_ACCESS_DEVICE; ;
                outputBuffer->DeviceTypeQualifier = 0;
                outputBuffer->PageCode = VPD_SUPPORTED_PAGES;
                outputBuffer->PageLength = 0x06;        // supports 6 VPD pages
                outputBuffer->SupportedPageList[0] = VPD_SUPPORTED_PAGES;
                outputBuffer->SupportedPageList[1] = VPD_SERIAL_NUMBER;
                outputBuffer->SupportedPageList[2] = VPD_ATA_INFORMATION;
                outputBuffer->SupportedPageList[3] = VPD_BLOCK_LIMITS;
                outputBuffer->SupportedPageList[4] = VPD_BLOCK_DEVICE_CHARACTERISTICS;
                outputBuffer->SupportedPageList[5] = VPD_LOGICAL_BLOCK_PROVISIONING;

                Srb->DataTransferLength = 0x0a;

                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                status = STOR_STATUS_SUCCESS;
            }
            break;
        }
        case VPD_BLOCK_LIMITS: {
            if (Srb->DataTransferLength < 0x14) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                status = STOR_STATUS_INVALID_PARAMETER;
            } else {
                PVPD_BLOCK_LIMITS_PAGE outputBuffer = (PVPD_BLOCK_LIMITS_PAGE)Srb->DataBuffer;

                outputBuffer->DeviceType = ChannelExtension->DeviceExtension->DeviceParameters.ScsiDeviceType; //DIRECT_ACCESS_DEVICE; ;
                outputBuffer->DeviceTypeQualifier = 0;
                outputBuffer->PageCode = VPD_BLOCK_LIMITS;

                //
                // leave outputBuffer->Descriptors[0 : 15] as '0' indicating 'not supported' for those fields.
                //

                if ( (Srb->DataTransferLength >= 0x24) &&
                     IsDeviceSupportsTrim(ChannelExtension) ) {
                    // not worry about multiply overflow as max of DsmCapBlockCount is min(AHCI_MAX_TRANSFER_LENGTH / ATA_BLOCK_SIZE, 0xFFFF)
                    // calculate how many LBA ranges can be associated with one DSM - Trim command
                    ULONG   maxLbaRangeEntryCountPerCmd = ChannelExtension->DeviceExtension[0].DeviceParameters.DsmCapBlockCount * (ATA_BLOCK_SIZE / sizeof(ATA_LBA_RANGE));
                    // calculate how many LBA can be associated with one DSM - Trim command
                    ULONG   maxLbaCountPerCmd = maxLbaRangeEntryCountPerCmd * MAX_ATA_LBA_RANGE_SECTOR_COUNT_VALUE;

                    NT_ASSERT (maxLbaCountPerCmd > 0);

                    // buffer is big enough for UNMAP information.
                    outputBuffer->PageLength[1] = 0x3C;        // must be 0x3C per spec

                    // (16:19) MAXIMUM UNMAP LBA COUNT
                    REVERSE_BYTES(&outputBuffer->Descriptors[16], &maxLbaCountPerCmd);

                    // (20:23) MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT
                    REVERSE_BYTES(&outputBuffer->Descriptors[20], &maxLbaRangeEntryCountPerCmd);

                    // (24:27) OPTIMAL UNMAP GRANULARITY
                    // (28:31) UNMAP GRANULARITY ALIGNMENT; (28) bit7: UGAVALID
                        //leave '0' indicates un-supported.

                    // keep original 'Srb->DataTransferLength' value.
                } else {
                    outputBuffer->PageLength[1] = 0x10;
                    Srb->DataTransferLength = 0x14;
                }

                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                status = STOR_STATUS_SUCCESS;
            }
            break;
        }
        case VPD_BLOCK_DEVICE_CHARACTERISTICS: {
            if (Srb->DataTransferLength < 0x08) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                status = STOR_STATUS_INVALID_PARAMETER;
            } else {
                PVPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE outputBuffer = (PVPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE)Srb->DataBuffer;

                outputBuffer->DeviceType = ChannelExtension->DeviceExtension->DeviceParameters.ScsiDeviceType; //DIRECT_ACCESS_DEVICE; ;
                outputBuffer->DeviceTypeQualifier = 0;
                outputBuffer->PageCode = VPD_BLOCK_DEVICE_CHARACTERISTICS;
                outputBuffer->PageLength = 0x3C;        // must be 0x3C per spec
                outputBuffer->MediumRotationRateMsb = (UCHAR)((ChannelExtension->DeviceExtension->IdentifyDeviceData->NominalMediaRotationRate >> 8) & 0x00FF);
                outputBuffer->MediumRotationRateLsb = (UCHAR)(ChannelExtension->DeviceExtension->IdentifyDeviceData->NominalMediaRotationRate & 0x00FF);
                outputBuffer->NominalFormFactor = (UCHAR)(ChannelExtension->DeviceExtension->IdentifyDeviceData->NominalFormFactor);

                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                status = STOR_STATUS_SUCCESS;
            }
            break;
        }
        case VPD_LOGICAL_BLOCK_PROVISIONING: {
            if (Srb->DataTransferLength < 0x08) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                status = STOR_STATUS_INVALID_PARAMETER;
            } else {
                PVPD_LOGICAL_BLOCK_PROVISIONING_PAGE outputBuffer = (PVPD_LOGICAL_BLOCK_PROVISIONING_PAGE)Srb->DataBuffer;

                outputBuffer->DeviceType = ChannelExtension->DeviceExtension->DeviceParameters.ScsiDeviceType; //DIRECT_ACCESS_DEVICE; ;
                outputBuffer->DeviceTypeQualifier = 0;
                outputBuffer->PageCode = VPD_LOGICAL_BLOCK_PROVISIONING;
                outputBuffer->PageLength[1] = 0x04;      // 8 bytes data in total
                outputBuffer->DP = 0;

                if (ChannelExtension->DeviceExtension->IdentifyDeviceData->AdditionalSupported.DeterministicReadAfterTrimSupported == TRUE) {
                    outputBuffer->ANC_SUP = IsDeviceSupportsTrim(ChannelExtension) ? 1 : 0;
                    outputBuffer->LBPRZ = ChannelExtension->DeviceExtension->IdentifyDeviceData->AdditionalSupported.ReadZeroAfterTrimSupported ? 1 : 0;
                } else {
                    outputBuffer->ANC_SUP = 0;
                    outputBuffer->LBPRZ = 0;
                }

                outputBuffer->LBPWS10 = 0;               // does not support WRITE SAME(10)
                outputBuffer->LBPWS = 0;                 // does not support WRITE SAME
                outputBuffer->LBPU = IsDeviceSupportsTrim(ChannelExtension) ? 1 : 0;

                Srb->DataTransferLength = 0x08;

                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                status = STOR_STATUS_SUCCESS;
            }
            break;
        }
        case VPD_SERIAL_NUMBER: {
            if (Srb->DataTransferLength < 24) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                status = STOR_STATUS_INVALID_PARAMETER;
            } else {
                PVPD_SERIAL_NUMBER_PAGE outputBuffer = (PVPD_SERIAL_NUMBER_PAGE)Srb->DataBuffer;
                UCHAR i;

                outputBuffer->DeviceType = ChannelExtension->DeviceExtension->DeviceParameters.ScsiDeviceType; //DIRECT_ACCESS_DEVICE; ;
                outputBuffer->DeviceTypeQualifier = 0;
                outputBuffer->PageCode = VPD_SERIAL_NUMBER;
                outputBuffer->PageLength = 20;      // 24 bytes data in total

                for (i=0; i<outputBuffer->PageLength; i+=2) {
                    REVERSE_BYTES_SHORT(&outputBuffer->SerialNumber[i], &ChannelExtension->DeviceExtension->IdentifyDeviceData->SerialNumber[i]);
                }

                Srb->DataTransferLength = outputBuffer->PageLength + 4;

                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                status = STOR_STATUS_SUCCESS;
            }
            break;
        }
        case VPD_ATA_INFORMATION: {
            if (Srb->DataTransferLength < sizeof(VPD_ATA_INFORMATION_PAGE)) {
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                status = STOR_STATUS_INVALID_PARAMETER;
            } else {
                PVPD_ATA_INFORMATION_PAGE outputBuffer = (PVPD_ATA_INFORMATION_PAGE)Srb->DataBuffer;
                ULONG vendorIdLength;
                ULONG len;
                ULONG prodLen;

                AhciZeroMemory((PCHAR)outputBuffer, sizeof(VPD_ATA_INFORMATION_PAGE));

                outputBuffer->DeviceType = ChannelExtension->DeviceExtension->DeviceParameters.ScsiDeviceType; //DIRECT_ACCESS_DEVICE; ;
                outputBuffer->DeviceTypeQualifier = 0;
                outputBuffer->PageCode = VPD_ATA_INFORMATION;
                outputBuffer->PageLength[0] = 0x02;
                outputBuffer->PageLength[1] = 0x38;      //PageLength: 0x238 - fixed value

                // if there is blank space in first 8 chars, use the part before blank space as VendorId
                for (vendorIdLength = 8; vendorIdLength > 0; vendorIdLength--) {
                    if (ChannelExtension->DeviceExtension->DeviceParameters.VendorId[vendorIdLength - 1] == ' ') {
                        break;
                    }
                }

                len = min(vendorIdLength, sizeof(ChannelExtension->DeviceExtension->DeviceParameters.VendorId));

                AhciFillMemory((PCHAR)outputBuffer->VendorId, 8, ' ');

                // if there is no blank space in first 8 chars, leave blank spaces in VendorId. Otherwise, copy the string
                if (len > 0 && len < 9) {
                    StorPortCopyMemory(outputBuffer->VendorId,
                                       ChannelExtension->DeviceExtension->DeviceParameters.VendorId,
                                       len
                                       );
                }

                prodLen = min(16, sizeof(ChannelExtension->DeviceExtension->DeviceParameters.VendorId) - len);

                AhciFillMemory((PCHAR)outputBuffer->ProductId, 16, ' ');
                StorPortCopyMemory(outputBuffer->ProductId,
                                   (ChannelExtension->DeviceExtension->DeviceParameters.VendorId + len),
                                   prodLen
                                   );

                len = min(4, sizeof(ChannelExtension->DeviceExtension->DeviceParameters.RevisionId));

                AhciFillMemory((PCHAR)outputBuffer->ProductRevisionLevel, 4, ' ');
                StorPortCopyMemory(outputBuffer->ProductRevisionLevel,
                                   ChannelExtension->DeviceExtension->DeviceParameters.RevisionId,
                                   len
                                   );

                //outputBuffer->DeviceSignature     -- not supported in current version of StorAHCI
                outputBuffer->CommandCode = IsAtaDevice(&ChannelExtension->DeviceExtension->DeviceParameters) ? IDE_COMMAND_IDENTIFY : IDE_COMMAND_ATAPI_IDENTIFY;
                StorPortCopyMemory(outputBuffer->IdentifyDeviceData, ChannelExtension->DeviceExtension->IdentifyDeviceData, sizeof(IDENTIFY_DEVICE_DATA));

                Srb->DataTransferLength = sizeof(VPD_ATA_INFORMATION_PAGE);

                Srb->SrbStatus = SRB_STATUS_SUCCESS;
                status = STOR_STATUS_SUCCESS;
            }
            break;
        }

        default:
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            status = STOR_STATUS_INVALID_PARAMETER;
            break;
        }
    }

    return status;
}

ULONG
AtaReportLunsCommand(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PVOID Context
    )
/*
Assumption: there will be no REPORT LUNS command received when the previous REPORT LUNS command is still in process.

  This function is used to respond to QDR.
  It send Identify (Device) command to device
*/
{
    ULONG status = STOR_STATUS_SUCCESS;
    PSCSI_REQUEST_BLOCK srb = (PSCSI_REQUEST_BLOCK)Context;

    // Filter out all TIDs but 0 since this is an AHCI interface without Port Multiplier which can only support one device.
    if (srb->Lun != 0) {
        // Indicate no device found at this address.
        srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
        status = STOR_STATUS_INVALID_PARAMETER;
    } else {
        IssueIdentifyCommand(ChannelExtension, srb);
    }

    return status;
}


ULONG
AtaStartStopUnitRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);
    PCDB                cdb = (PCDB)Srb->Cdb;

    UNREFERENCED_PARAMETER(ChannelExtension);

    //for STOP UNIT or EJECT MEDIA
    if (cdb->START_STOP.Start == 0) {

        srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;

        if (cdb->START_STOP.LoadEject == 1) {
            SetCommandReg((&srbExtension->TaskFile.Current), IDE_COMMAND_MEDIA_EJECT);
        } else {
            SetCommandReg((&srbExtension->TaskFile.Current), IDE_COMMAND_STANDBY_IMMEDIATE);
        }
    } else {
        //no action required for AHCI
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

    return STOR_STATUS_SUCCESS;
}

ULONG
AtaTestUnitReadyRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    if (IsMsnEnabled(ChannelExtension->DeviceExtension)) {
        // this command does not transfer data
        srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;

        SetCommandReg((&srbExtension->TaskFile.Current), IDE_COMMAND_GET_MEDIA_STATUS);
    } else {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

    return STOR_STATUS_SUCCESS;
}

ULONG
AtaMediumRemovalRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);
    PCDB                cdb = (PCDB)Srb->Cdb;

    if (IsRmfEnabled(ChannelExtension->DeviceExtension)) {
        // this command does not transfer data
        UCHAR commandReg;
        commandReg = (cdb->MEDIA_REMOVAL.Prevent == 1) ? IDE_COMMAND_DOOR_LOCK : IDE_COMMAND_DOOR_UNLOCK;

        srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;

        SetCommandReg((&srbExtension->TaskFile.Current), commandReg);
    } else {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

    return STOR_STATUS_SUCCESS;
}

VOID
AtaAlwaysSuccessRequestCompletion (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    UNREFERENCED_PARAMETER(ChannelExtension);

    if (SRB_STATUS(Srb->SrbStatus) != SRB_STATUS_SUCCESS) {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    }

    return;
}

ULONG
AtaFlushCommandRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    if (NoFlushDevice(ChannelExtension->DeviceExtension)) {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    } else {
        //
        UCHAR commandReg;
        commandReg = Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters) ? IDE_COMMAND_FLUSH_CACHE_EXT : IDE_COMMAND_FLUSH_CACHE;

        srbExtension->AtaFunction = ATA_FUNCTION_ATA_FLUSH;
        srbExtension->CompletionRoutine = AtaAlwaysSuccessRequestCompletion;    //legacy behavior: Flush Command will be always completed successfully.

        if ((ChannelExtension->DeviceExtension->DeviceParameters.StateFlags.SystemPoweringDown == TRUE) ) {
            //Final Flush goes down alone
            if (ChannelExtension->SlotManager.NormalQueueSlice != 0) {
                NT_ASSERT(FALSE);
            }
            //This is the last FLUSH.  No more IO.
            ChannelExtension->StateFlags.NoMoreIO = TRUE;
        }

        SetCommandReg((&srbExtension->TaskFile.Current), commandReg);
    }

    return STOR_STATUS_SUCCESS;
}

VOID
AtaPassThroughRequestCompletion (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    UNREFERENCED_PARAMETER(ChannelExtension);

    if (SRB_STATUS(Srb->SrbStatus) != SRB_STATUS_SUCCESS) {
        // keep SRB_STATUS_AUTOSENSE_VALID it's set by StorAHCI
        // this flag is checked by Storport to copy back SenseInfoBuffer to original sense buffer.
        Srb->SrbStatus = SRB_STATUS_SUCCESS | (Srb->SrbStatus & SRB_STATUS_AUTOSENSE_VALID);
    }

    return;
}


ULONG
AtaPassThroughRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);
    PCDB                cdb = (PCDB)Srb->Cdb;
    
    UNREFERENCED_PARAMETER(ChannelExtension);

    // if it's a 48bit command but device doesn't support it, assert.
    // command issuer needs to make sure this doesn't happen. The command will be sent to device and let device fail it, so that there is ATA status and error returned to issuer.
    NT_ASSERT( Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters) || (cdb->ATA_PASSTHROUGH16.Extend == 0) );

    srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;

    if (cdb->ATA_PASSTHROUGH16.CkCond == 1) {
        srbExtension->Flags |= ATA_FLAGS_RETURN_RESULTS;    // indicate task file content needs to be returned in SenseInfoBuffer
    }

    if (Srb->SrbFlags & SRB_FLAGS_DATA_IN) {
        srbExtension->Flags |= ATA_FLAGS_DATA_IN;
    }
    if (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) {
        srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
    }

    if ( Support48Bit(&ChannelExtension->DeviceExtension->DeviceParameters) && (cdb->ATA_PASSTHROUGH16.Extend == 1) ) {
        srbExtension->Flags |= ATA_FLAGS_48BIT_COMMAND;
    }

    // ATA command taskfile
    srbExtension->TaskFile.Current.bFeaturesReg = cdb->ATA_PASSTHROUGH16.Features7_0;
    srbExtension->TaskFile.Current.bSectorCountReg = cdb->ATA_PASSTHROUGH16.SectorCount7_0;
    srbExtension->TaskFile.Current.bSectorNumberReg = cdb->ATA_PASSTHROUGH16.LbaLow7_0;
    srbExtension->TaskFile.Current.bCylLowReg = cdb->ATA_PASSTHROUGH16.LbaMid7_0;
    srbExtension->TaskFile.Current.bCylHighReg = cdb->ATA_PASSTHROUGH16.LbaHigh7_0;
    srbExtension->TaskFile.Current.bDriveHeadReg = cdb->ATA_PASSTHROUGH16.Device;
    srbExtension->TaskFile.Current.bCommandReg = cdb->ATA_PASSTHROUGH16.Command;

    srbExtension->TaskFile.Previous.bFeaturesReg =cdb->ATA_PASSTHROUGH16.Features15_8 ;
    srbExtension->TaskFile.Previous.bSectorCountReg = cdb->ATA_PASSTHROUGH16.SectorCount15_8;
    srbExtension->TaskFile.Previous.bSectorNumberReg = cdb->ATA_PASSTHROUGH16.LbaLow15_8;
    srbExtension->TaskFile.Previous.bCylLowReg = cdb->ATA_PASSTHROUGH16.LbaMid15_8;
    srbExtension->TaskFile.Previous.bCylHighReg = cdb->ATA_PASSTHROUGH16.LbaHigh15_8;
    srbExtension->TaskFile.Previous.bDriveHeadReg = 0;
    srbExtension->TaskFile.Previous.bCommandReg = 0;

    srbExtension->CompletionRoutine = AtaPassThroughRequestCompletion;

    return STOR_STATUS_SUCCESS;
}

ULONG
ConvertUnmapBlockDescrToAtaLbaRanges(
    _Inout_ PUNMAP_BLOCK_DESCRIPTOR BlockDescr,
    _In_reads_bytes_(BufferSize) PCHAR DestBuffer,
    _In_ ULONG  BufferSize
    )
/*++

Routine Description:

    Convert UNMAP_BLOCK_DESCRIPTOR entry to be ATA_LBA_RANGE entries.

    UNMAP_BLOCK_DESCRIPTOR->LbaCount is 32 bits; ATA_LBA_RANGE->SectorCount is 16 bits.
    it's possible that one UNMAP_BLOCK_DESCRIPTOR entry needs multiple ATA_LBA_RANGE entries.

Arguments:

    BlockDescr - the UNMAP_BLOCK_DESCRIPTOR entry, value of fields will be updated in this function
    DestBuffer
    BufferSize

Return Value:

    Count of ATA_LBA_RANGE entries converted.

    NOTE: if UNMAP_BLOCK_DESCRIPTOR->LbaCount does not reach to 0, the conversion for UNMAP_BLOCK_DESCRIPTOR entry
          is not completed. Further conversion is needed by calling this function again.

Example:  Lba - 0Bh, Length - 08h
ATA:          0008_0000_0000_000B
      In memory(little-endian):  0B 00 00 00 00 00 - 08 00
SCSI:         0000_0000 0800_0000 0B00_0000_0000_0000
      In memory(little-endian):  00 00 00 00 00 00 00 0B - 00 00 00 08 - 00 00 00 00

--*/
{
    ULONG           convertedEntryCount;
    PATA_LBA_RANGE  lbaRangeEntry;

    ULONGLONG       blockDescrStartingLba;
    ULONG           blockDescrLbaCount;

    convertedEntryCount = 0;
    lbaRangeEntry = (PATA_LBA_RANGE)DestBuffer;

    REVERSE_BYTES_QUAD(&blockDescrStartingLba, BlockDescr->StartingLba);
    REVERSE_BYTES(&blockDescrLbaCount, BlockDescr->LbaCount);

    // 1. fill in ATA_LBA_RANGE entries as needed
    while ((blockDescrLbaCount > 0) &&
           (convertedEntryCount * sizeof(ATA_LBA_RANGE) < BufferSize)) {
        //
        ULONG sectorCount;

        if (blockDescrLbaCount > MAX_ATA_LBA_RANGE_SECTOR_COUNT_VALUE) {
            sectorCount = MAX_ATA_LBA_RANGE_SECTOR_COUNT_VALUE;
        } else {
            sectorCount = blockDescrLbaCount;
        }

        lbaRangeEntry[convertedEntryCount].StartSector = blockDescrStartingLba;
        lbaRangeEntry[convertedEntryCount].SectorCount = sectorCount;

        blockDescrStartingLba += sectorCount;
        blockDescrLbaCount -= sectorCount;

        convertedEntryCount++;
    }

    // 2. update the UNMAP Block Descriptor
    REVERSE_BYTES_QUAD(BlockDescr->StartingLba, &blockDescrStartingLba);
    REVERSE_BYTES(BlockDescr->LbaCount, &blockDescrLbaCount);

    return convertedEntryCount;
}

ULONG
__inline
GetDataBufferLengthForDsmCommand (
    _In_ ULONG MaxLbaRangeEntryCountPerCmd,
    _In_ ULONG NeededLbaRangeEntryCount
)
/*
    Input:
        MaxLbaRangeEntryCountPerCmd:
        NeededLbaRangeEntryCount:

    Return value:
        the size of buffer needed for DSM command, the size is rounded up to ATA Lba range block size (512 byes).

    Note that the buffer needs to be rounded up to ATA Lba Range block size (512 byes).
    also Note that data transfer length is much smaller than 4G, the following multiplications are safe.
*/
{
    ULONG bufferLength;

    if (NeededLbaRangeEntryCount >= MaxLbaRangeEntryCountPerCmd) {
        // 1 use the max buffer size for a DSM command
        bufferLength = MaxLbaRangeEntryCountPerCmd * sizeof(ATA_LBA_RANGE);
    }
    else {
        // 2 one single DSM command can satisfy the Unmap request
        bufferLength = NeededLbaRangeEntryCount * sizeof(ATA_LBA_RANGE);
    }

    // 3 buffer size round up to ATA Lba range block size (512 byes).
    bufferLength = ((bufferLength - 1) / ATA_BLOCK_SIZE + 1) * ATA_BLOCK_SIZE;

    return bufferLength;
}

VOID
DeviceProcessTrimRequest(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
)
/*++

Routine Description:

    Process TRIM request that received from upper layer.

Arguments:

    ChannelExtension
    Srb

Return Value:

    status of the operation

--*/
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);
    BOOLEAN             completed = FALSE;

    PATA_TRIM_CONTEXT   trimContext = (PATA_TRIM_CONTEXT)srbExtension->CompletionContext;
    PCHAR               buffer = (PCHAR)srbExtension->DataBuffer;
    ULONG               bufferLength = 0;

    BOOLEAN             tempBlockDescrConverted = TRUE;
    ULONG               bufferLengthUsed = 0;
    ULONG               convertedEntryCount = 0;

    completed = ((Srb->SrbStatus != SRB_STATUS_PENDING) && (Srb->SrbStatus != SRB_STATUS_SUCCESS)) ||
                (trimContext->ProcessedLbaRangeEntryCount >= trimContext->NeededLbaRangeEntryCount);

    if (!completed) {
        AhciZeroMemory(buffer, trimContext->AllocatedBufferLength);

        // 1. calculate the buffer size needed for DSM command
        bufferLength = GetDataBufferLengthForDsmCommand(trimContext->MaxLbaRangeEntryCountPerCmd, trimContext->NeededLbaRangeEntryCount - trimContext->ProcessedLbaRangeEntryCount);

        // 2. prepare and send DSM command
        //    check if the Unmap request is satisfied
        while (trimContext->ProcessedLbaRangeEntryCount < trimContext->NeededLbaRangeEntryCount) {
            // when first time this function is called, trimContext->CurrentBlockDescr.LbaCount should be '0'
            // so that the first Block Descriptor will be copied to trimContext->CurrentBlockDescr
            ULONG   tempBlockDescrLbaCount;

            REVERSE_BYTES(&tempBlockDescrLbaCount, trimContext->CurrentBlockDescr.LbaCount);
            tempBlockDescrConverted = (tempBlockDescrLbaCount == 0)? TRUE : FALSE;

            // if the previous entry conversion completed, continue the next one;
            // otherwise, still process the left part of the un-completed entry.
            if (tempBlockDescrConverted) {
                StorPortCopyMemory(&trimContext->CurrentBlockDescr, &trimContext->BlockDescriptors[trimContext->BlockDescrIndex], sizeof(UNMAP_BLOCK_DESCRIPTOR));
                trimContext->BlockDescrIndex++;
            }

            convertedEntryCount = ConvertUnmapBlockDescrToAtaLbaRanges(&trimContext->CurrentBlockDescr,
                                                                       buffer + bufferLengthUsed,
                                                                       bufferLength - bufferLengthUsed
                                                                       );
            trimContext->ProcessedLbaRangeEntryCount += convertedEntryCount;

            bufferLengthUsed += convertedEntryCount * sizeof(ATA_LBA_RANGE);

            // send DSM trim command when the buffer is full or all unmap entries are converted.
            if ( (bufferLengthUsed == bufferLength) ||
                 (trimContext->ProcessedLbaRangeEntryCount >= trimContext->NeededLbaRangeEntryCount) ) {
                // get ATA block count, the value is needed for setting the DSM command.
                ULONG transferBlockCount = bufferLength / ATA_BLOCK_SIZE;

                srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
                srbExtension->DataBuffer = buffer;
                srbExtension->DataTransferLength = bufferLength;

                // ATA command taskfile
                AhciZeroMemory((PCHAR)&srbExtension->TaskFile, sizeof(ATA_TASK_FILE));

                srbExtension->TaskFile.Current.bFeaturesReg = IDE_DSM_FEATURE_TRIM;
                // For TRIM command: LBA bit (bit 6) needs to be set for Device Register;
                // bit 7 and bit 5 are obsolete and always set by ATAport;
                // bit 4 is to select device0 or device1
                srbExtension->TaskFile.Current.bDriveHeadReg = 0xE0;
                srbExtension->TaskFile.Current.bCommandReg = IDE_COMMAND_DATA_SET_MANAGEMENT;

                srbExtension->TaskFile.Current.bSectorCountReg = (UCHAR)(0x00FF & transferBlockCount);
                srbExtension->TaskFile.Previous.bSectorCountReg = (UCHAR)(transferBlockCount >> 8);

                srbExtension->CompletionRoutine = DeviceProcessTrimRequest;
                srbExtension->CompletionContext = (PVOID)trimContext;

                // update the SGL to reflact the actual transfer length.
                srbExtension->LocalSgl.List[0].Length = bufferLength;

                return;
            }
        }
        // should not reach this path
        NT_ASSERT(FALSE);

    } else {
        if (buffer != NULL) {
            AhciFreeDmaBuffer((PVOID)ChannelExtension->AdapterExtension, trimContext->AllocatedBufferLength, buffer);
        }

        if (trimContext != NULL) {
            StorPortFreePool((PVOID)ChannelExtension->AdapterExtension, trimContext);
        }

        srbExtension->DataBuffer = NULL;
        srbExtension->CompletionContext = NULL;
        srbExtension->CompletionRoutine = NULL;
        srbExtension->AtaFunction = 0;
    }
    return;
}


ULONG
AtaUnmapRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    ULONG               status = STOR_STATUS_SUCCESS;
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    PUNMAP_LIST_HEADER  unmapList = NULL;
    USHORT              blockDescrDataLength = 0;

    PATA_TRIM_CONTEXT   trimContext = NULL;
    PCHAR               buffer = NULL;     // buffer allocated for DSM trim command

    unmapList = (PUNMAP_LIST_HEADER)(Srb->DataBuffer);

    REVERSE_BYTES_SHORT(&blockDescrDataLength, unmapList->BlockDescrDataLength);

    if ( !IsDeviceSupportsTrim(ChannelExtension) ||
         (Srb->DataTransferLength < (ULONG)(blockDescrDataLength + 8)) ) {
        // either trim is not supported; or the Block Descriptor Size is too big
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STOR_STATUS_INVALID_PARAMETER;
    } else {
        // some preparation work before actually starting to process the request
        ULONG                 i = 0;
        ULONG                 length = 0;
        STOR_PHYSICAL_ADDRESS bufferPhysicalAddress;

        status = StorPortAllocatePool(ChannelExtension->AdapterExtension, sizeof(ATA_TRIM_CONTEXT), AHCI_POOL_TAG, (PVOID*)&trimContext);
        if ( (status != STOR_STATUS_SUCCESS) || (trimContext == NULL) ) {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            if (status == STOR_STATUS_SUCCESS) {
                status = STOR_STATUS_INSUFFICIENT_RESOURCES;
            }
            goto Exit;
        }
        AhciZeroMemory((PCHAR)trimContext, sizeof(ATA_TRIM_CONTEXT));

        trimContext->BlockDescriptors = (PUNMAP_BLOCK_DESCRIPTOR)((PCHAR)Srb->DataBuffer + 8);
        trimContext->BlockDescrCount = blockDescrDataLength / sizeof(UNMAP_BLOCK_DESCRIPTOR);

        // 1.1 calculate how many ATA Lba entries can be sent per DSM command
        //     every device LBA entry takes 8 bytes. not worry about multiply overflow as max of DsmCapBlockCount is 0xFFFF
        trimContext->MaxLbaRangeEntryCountPerCmd = (ChannelExtension->DeviceExtension[0].DeviceParameters.DsmCapBlockCount * ATA_BLOCK_SIZE) / sizeof(ATA_LBA_RANGE);

        if (trimContext->MaxLbaRangeEntryCountPerCmd == 0) {
            // do not expect this to happen.
            NT_ASSERT(FALSE);
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            status = STOR_STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        // 1.2 calculate how many ATA Lba entries needed to complete this Unmap request
        for (i = 0; i < trimContext->BlockDescrCount; i++) {
            ULONG blockDescrLbaCount;
            REVERSE_BYTES(&blockDescrLbaCount, trimContext->BlockDescriptors[i].LbaCount);
            // 1.2.1 the ATA Lba entry - SectorCount field is 16bits; the Unmap Lba entry - LbaCount field is 32bits.
            //       following calculation shows how many ATA Lba entries should be used to represent the Unmap Lba entry.
            if (blockDescrLbaCount > 0) {
                trimContext->NeededLbaRangeEntryCount += (blockDescrLbaCount - 1) / MAX_ATA_LBA_RANGE_SECTOR_COUNT_VALUE + 1;
            }
        }

        // 1.3 calculate the buffer size needed for DSM command
        trimContext->AllocatedBufferLength = GetDataBufferLengthForDsmCommand(trimContext->MaxLbaRangeEntryCountPerCmd, trimContext->NeededLbaRangeEntryCount);

        if (trimContext->AllocatedBufferLength == 0) {
            // UNMAP without Block Descriptor is allowed, SBC spec requires to not consider this as error.
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            status = STOR_STATUS_SUCCESS;
            goto Exit;
        }

        // 1.4 allocate buffer, this buffer will be used to store ATA LBA Ranges for DSM command
        status = AhciAllocateDmaBuffer((PVOID)ChannelExtension->AdapterExtension, trimContext->AllocatedBufferLength, (PVOID*)&buffer);

        if ( (status != STOR_STATUS_SUCCESS) || (buffer == NULL) ) {
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            if (status == STOR_STATUS_SUCCESS) {
                status = STOR_STATUS_INSUFFICIENT_RESOURCES;
            }
            goto Exit;
        }

        // save values before calling DeviceProcessTrimRequest()
        srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
        srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
        srbExtension->DataBuffer = buffer;
        srbExtension->DataTransferLength = trimContext->AllocatedBufferLength;
        srbExtension->CompletionContext = (PVOID)trimContext;

        bufferPhysicalAddress = StorPortGetPhysicalAddress(ChannelExtension->AdapterExtension, NULL, buffer, &length);
        srbExtension->LocalSgl.NumberOfElements = 1;
        srbExtension->LocalSgl.List[0].PhysicalAddress.LowPart = bufferPhysicalAddress.LowPart;
        srbExtension->LocalSgl.List[0].PhysicalAddress.HighPart = bufferPhysicalAddress.HighPart;
        srbExtension->LocalSgl.List[0].Length = trimContext->AllocatedBufferLength;
        srbExtension->Sgl = &srbExtension->LocalSgl;

        // process the request, this function will set itself as completion routine to send multiple DSM commands one by one.
        DeviceProcessTrimRequest(ChannelExtension, Srb);
    }

Exit:
    // the process failed before DSM command can be sent. Free allocated resources.
    if (status != STOR_STATUS_SUCCESS) {
        if (buffer != NULL) {
            AhciFreeDmaBuffer((PVOID)ChannelExtension->AdapterExtension, trimContext->AllocatedBufferLength, buffer);
        }

        if (trimContext != NULL) {
            StorPortFreePool((PVOID)ChannelExtension->AdapterExtension, trimContext);
        }
    }

    return status;
}



ULONG
AtaSecurityProtocolRequest (
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);
    PCDB                securityCdb = (PCDB)Srb->Cdb;
    ULONG               dataLength = 0;
    UCHAR               commandReg;
    UCHAR               nonDataTrustedReceive = 0;

    if (ChannelExtension->DeviceExtension->IdentifyDeviceData->TrustedComputing.FeatureSupported == 0) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
    } else if (securityCdb->SECURITY_PROTOCOL_IN.INC_512 == 0) {
        // Reject the command if INC_512 bit is set to 0. Some drivers use this answer to know the transfer size should be aligned to 512.
        // StorAHCI only supports the command with INC_512 bit set to 1, to avoid the complexity of handling partial sectors.
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
    } else {

        dataLength = (securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[0] << 24) |
                     (securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[0] << 16) |
                     (securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[0] << 8) |
                     (securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[0]);

        if (dataLength > 0xFFFF) {
            // ATA TRUSTED commands can only process 2 bytes of data transfter length.
            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        } else {
            // get command to be used
            if ((securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[3] == 0) &&
                (securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[2] == 0) &&
                (securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[1] == 0) &&
                (securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[0] == 0)) {
                // Non-data transfer
                commandReg = IDE_COMMAND_TRUSTED_NON_DATA;
                nonDataTrustedReceive = (Srb->Cdb[0] == SCSIOP_SECURITY_PROTOCOL_IN) ? 1 : 0;
            } else {
                NT_ASSERT((Srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) != 0);
                if (Srb->Cdb[0] == SCSIOP_SECURITY_PROTOCOL_IN) {
                    commandReg = IDE_COMMAND_TRUSTED_RECEIVE_DMA;
                    srbExtension->Flags |= ATA_FLAGS_DATA_IN;
                } else {
                    commandReg = IDE_COMMAND_TRUSTED_SEND_DMA;
                    srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
                }
                srbExtension->Flags |= ATA_FLAGS_USE_DMA;
            }

            // Set up taskfile in irb.
            srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;

            SetCommandReg((&srbExtension->TaskFile.Current), commandReg);
            SetFeaturesReg((&srbExtension->TaskFile.Current), securityCdb->SECURITY_PROTOCOL_IN.SecurityProtocol);

            SetSectorCount((&srbExtension->TaskFile.Current), securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[3]);           //low byte of transfer length
            SetSectorNumber((&srbExtension->TaskFile.Current), securityCdb->SECURITY_PROTOCOL_IN.AllocationLength[2]);          //high byte of transfer length

            SetCylinderHigh((&srbExtension->TaskFile.Current), securityCdb->SECURITY_PROTOCOL_IN.SecurityProtocolSpecific[0]);  //SILO_INDEX, high byte of protocol specific
            SetCylinderLow((&srbExtension->TaskFile.Current), securityCdb->SECURITY_PROTOCOL_IN.SecurityProtocolSpecific[1]);   //FUNCTION_ID, low byte of protocol specific

            SetDeviceReg((&srbExtension->TaskFile.Current), nonDataTrustedReceive);
        }
    }

    return STOR_STATUS_SUCCESS;
}

UCHAR
AtaMapError(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ BOOLEAN FUAcommand
    )
/*++

Routine Description:

    Maps the ATA errors to SCSI and builds sense data for them

Arguments:

    ChannelExtension
    Srb

Return Value:

    SrbStatus

--*/

{
    BOOLEAN     removableMedia;
    SENSE_DATA  senseBuffer = {0};
    ULONG       length = sizeof(SENSE_DATA);

    PAHCI_SRB_EXTENSION srbExtension = GetSrbExtension(Srb);

    if (IsReturnResults(srbExtension->Flags)) {
        // There is already something in the SenseInfoBuffer or this is an ATA PASS THRU.
        return Srb->SrbStatus;
    }

    // 1. special interpretion for FUA command
    if (FUAcommand == TRUE) {
       //if the first FUA command succeeds, remember so future failures don't disable FUA commands
       //if the first FUA command fails, leverage the class driver to not send anymore FUA commands
        if (ChannelExtension->DeviceExtension->DeviceParameters.StateFlags.FuaSucceeded == 0) {
            if (Srb->SrbStatus == SRB_STATUS_SUCCESS) {
                ChannelExtension->DeviceExtension->DeviceParameters.StateFlags.FuaSucceeded = 1;
            } else if (Srb->SrbStatus == SRB_STATUS_ERROR) {
                //
                Srb->SrbStatus = SRB_STATUS_ERROR;
                Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

                senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
                senseBuffer.Valid     = 1;
                senseBuffer.AdditionalSenseLength = 0xb;
                senseBuffer.SenseKey =  SCSI_SENSE_ILLEGAL_REQUEST;
                senseBuffer.AdditionalSenseCode = SCSI_ADSENSE_INVALID_CDB;
                senseBuffer.AdditionalSenseCodeQualifier = 0;
            }
        }
    }

    if (Srb->SrbStatus != SRB_STATUS_ERROR) {
        // non device errors. Don't care
        ChannelExtension->DeviceExtension[0].IoRecord.SuccessCount++;

        return Srb->SrbStatus;
    }

    // 2. general process
    removableMedia = IsRemovableMedia(&ChannelExtension->DeviceExtension->DeviceParameters);

    if (senseBuffer.Valid == 0) {
        // senseBuffer is not set yet, start ...

        if (srbExtension->AtaError & IDE_ERROR_CRC_ERROR) {
            // bit 7: Interface CRC error

            Srb->SrbStatus = SRB_STATUS_PARITY_ERROR;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey =  SCSI_SENSE_HARDWARE_ERROR;
            senseBuffer.AdditionalSenseCode = SCSI_ADSENSE_LUN_COMMUNICATION;
            senseBuffer.AdditionalSenseCodeQualifier = SCSI_SESNEQ_COMM_CRC_ERROR;

            ChannelExtension->DeviceExtension[0].IoRecord.CrcErrorCount++;

        } else if (srbExtension->AtaError & IDE_ERROR_DATA_ERROR) {
            // bit 6: Uncorrectable Error

            Srb->SrbStatus = SRB_STATUS_ERROR;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey = removableMedia ? SCSI_SENSE_DATA_PROTECT :
                                                    SCSI_SENSE_MEDIUM_ERROR ;
            senseBuffer.AdditionalSenseCode = 0;
            senseBuffer.AdditionalSenseCodeQualifier = 0;

            if (removableMedia) {
                ChannelExtension->DeviceExtension[0].IoRecord.OtherErrorCount++;
            } else {
                ChannelExtension->DeviceExtension[0].IoRecord.MediaErrorCount++;
            }

        } else if (srbExtension->AtaError & IDE_ERROR_MEDIA_CHANGE) {
            // bit 5: Media Changed (legacy)

            Srb->SrbStatus = SRB_STATUS_ERROR;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey =  SCSI_SENSE_UNIT_ATTENTION;
            senseBuffer.AdditionalSenseCode = SCSI_ADSENSE_MEDIUM_CHANGED;
            senseBuffer.AdditionalSenseCodeQualifier = 0;

            ChannelExtension->DeviceExtension[0].IoRecord.OtherErrorCount++;

        } else if (srbExtension->AtaError & IDE_ERROR_ID_NOT_FOUND) {
            // bit 4: ID Not Found

            Srb->SrbStatus = SRB_STATUS_ERROR;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey =  SCSI_SENSE_ILLEGAL_REQUEST;
            senseBuffer.AdditionalSenseCode = SCSI_ADSENSE_ILLEGAL_BLOCK;
            senseBuffer.AdditionalSenseCodeQualifier = 0;

            ChannelExtension->DeviceExtension[0].IoRecord.IllegalCommandCount++;

        } else if (srbExtension->AtaError & IDE_ERROR_MEDIA_CHANGE_REQ) {
            // bit 3: Media Change Request (legacy)

            Srb->SrbStatus = SRB_STATUS_ERROR;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey =  SCSI_SENSE_UNIT_ATTENTION;
            senseBuffer.AdditionalSenseCode = SCSI_ADSENSE_OPERATOR_REQUEST;
            senseBuffer.AdditionalSenseCodeQualifier = SCSI_SENSEQ_MEDIUM_REMOVAL;

            ChannelExtension->DeviceExtension[0].IoRecord.OtherErrorCount++;

        } else if (srbExtension->AtaError & IDE_ERROR_COMMAND_ABORTED) {

            // bit 2: Command Aborted

            Srb->SrbStatus = SRB_STATUS_ABORTED;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey =  SCSI_SENSE_ABORTED_COMMAND;
            senseBuffer.AdditionalSenseCode = 0;
            senseBuffer.AdditionalSenseCodeQualifier = 0;

            ChannelExtension->DeviceExtension[0].IoRecord.AbortedCommandCount++;

        } else if (srbExtension->AtaError & IDE_ERROR_END_OF_MEDIA) {
            // bit 1: End of Media (legacy)

            Srb->SrbStatus = SRB_STATUS_ERROR;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey =  SCSI_SENSE_NOT_READY;
            senseBuffer.AdditionalSenseCode = SCSI_ADSENSE_NO_MEDIA_IN_DEVICE;
            senseBuffer.AdditionalSenseCodeQualifier = 0;

            ChannelExtension->DeviceExtension[0].IoRecord.EndofMediaCount++;

        } else if (srbExtension->AtaError & IDE_ERROR_ADDRESS_NOT_FOUND) {
            // bit 0: Media Error (legacy)

            Srb->SrbStatus = SRB_STATUS_ERROR;

            senseBuffer.ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
            senseBuffer.Valid     = 1;
            senseBuffer.AdditionalSenseLength = 0xb;
            senseBuffer.SenseKey = removableMedia ? SCSI_SENSE_DATA_PROTECT :
                                                    SCSI_SENSE_MEDIUM_ERROR ;
            senseBuffer.AdditionalSenseCode = 0;
            senseBuffer.AdditionalSenseCodeQualifier = 0;

            if (removableMedia) {
                ChannelExtension->DeviceExtension[0].IoRecord.OtherErrorCount++;
            } else {
                ChannelExtension->DeviceExtension[0].IoRecord.MediaErrorCount++;
            }

        } else {
            Srb->SrbStatus = SRB_STATUS_ERROR;
            ChannelExtension->DeviceExtension[0].IoRecord.OtherErrorCount++;
        }
    }

    if ( (senseBuffer.Valid == 1) && (Srb->SenseInfoBuffer) ) {

        if ( (Srb->SenseInfoBufferLength > 0) &&
             (Srb->SenseInfoBufferLength < length) ) {
            length = Srb->SenseInfoBufferLength;
        }

        NT_ASSERT(length > 0);
        NT_ASSERT(Srb->SenseInfoBuffer != NULL);

        StorPortCopyMemory(Srb->SenseInfoBuffer, (PVOID)&senseBuffer, length);

        Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    }

    return Srb->SrbStatus;
}


VOID
CopyField(
    _Out_writes_bytes_(Count+1) PUCHAR Destination,
    _In_reads_bytes_(Count) PUCHAR Source,
    _In_ ULONG Count,
    _In_ UCHAR Change
    )

/*++

Routine Description:

    This routine will copy Count string bytes from Source to Destination.  If
    it finds a null byte in the Source it will translate that and any subsequent
    bytes into Change.  It will also replace non-printable characters with the
    specified character.

Arguments:

    Destination - the location to copy bytes

    Source - the location to copy bytes from

    Count - the number of bytes to be copied

Return Value:

    none

Notes:

    This routine will add a NULL char at Destination[Count].
--*/

{
    ULONG i = 0;
    BOOLEAN pastEnd = FALSE;

    for(i = 0; i < Count; i++) {
        if(!pastEnd) {
            if(Source[i] == 0) {
                pastEnd = TRUE;
                Destination[i] = Change;
            } else if ((Source[i] <= ' ') ||
                       (Source[i] > 0x7f) ||
                       (Source[i] == ',')) {
                Destination[i] = Change;
            } else {
                Destination[i] = Source[i];
            }
        } else {
            Destination[i] = Change;
        }
    }

    Destination[i] = '\0';

    return;
}

VOID
FormatAtaId (
    _Out_writes_bytes_(CharCount + 1) PUCHAR Destination,
    _In_reads_bytes_(CharCount)  PUCHAR Source,
    _In_ ULONG CharCount
    )
/*++

    Constructs and formats ATA string. Used for ModelNumber, FirmwareRevision and SerialNumber

--*/
{
    NT_ASSERT(CharCount > 0);

    Destination[0] = '\0';

    CopyField(Destination, Source, CharCount, ' ');

    ByteSwap(Destination, CharCount);

    // This will null terminate the string
    RemoveTrailingBlanks(Destination, CharCount + 1);

    return;
}


VOID
DeviceInitAtaIds(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PIDENTIFY_DEVICE_DATA IdentifyDeviceData
    )
{
    FormatAtaId(ChannelExtension->DeviceExtension->DeviceParameters.VendorId,
                IdentifyDeviceData->ModelNumber,
                sizeof(ChannelExtension->DeviceExtension->DeviceParameters.VendorId) - 1
                );

    FormatAtaId(ChannelExtension->DeviceExtension->DeviceParameters.RevisionId,
                IdentifyDeviceData->FirmwareRevision,
                sizeof(ChannelExtension->DeviceExtension->DeviceParameters.RevisionId) - 1
                );

    FormatAtaId(ChannelExtension->DeviceExtension->DeviceParameters.SerialNumber,
                IdentifyDeviceData->SerialNumber,
                sizeof(ChannelExtension->DeviceExtension->DeviceParameters.SerialNumber) - 1
                );
    return;
}

VOID
FormatAtapiVendorId(
    _In_ PINQUIRYDATA InquiryData,
    _Out_writes_bytes_(Length) PUCHAR VendorId,
    _In_ ULONG Length
    )
/*++

Routine Description:

    Constructs and formats the Vendor  Id from InquiryData

Arguments:

    InquiryData - the InquiryData from the device
    VendorId - destination buffer
    Length - Length of the destination buffer

Return Value:

    None.

--*/
{
    ULONG index;
    ULONG bytesLeft;

    VendorId[0] = '\0';
    bytesLeft = Length;

    if (bytesLeft > 8) {

        CopyField( VendorId,
                   InquiryData->VendorId,
                   8,
                   ' '
                   );

        NT_ASSERT(VendorId[8] == '\0');
        VendorId[8] = ' ';

        index = RemoveTrailingBlanks(VendorId, 9);

        if (index < Length) {
            NT_ASSERT(VendorId[index] == '\0');

            VendorId[index] = ' ';
            VendorId += index + 1;

            bytesLeft -= (index + 1);
        }
    }

    if (bytesLeft > 16) {

// At this point, VendorId points a buffer that has at least 16 bytes
#pragma warning (suppress: 6386)
        CopyField( VendorId,
                   InquiryData->ProductId,
                   16,
                   ' '
                   );

        NT_ASSERT(VendorId[16] == '\0');
        VendorId[16] = ' ';

        RemoveTrailingBlanks(VendorId, 17);
    }

    return;
}

VOID
FormatAtapiRevisionId(
    _In_ PINQUIRYDATA InquiryData,
    _Out_writes_bytes_(Length) PUCHAR RevisionId,
    _In_ ULONG Length
    )
/*++

Routine Description:

    Constructs and formats the  Revision Id from InquiryData

Arguments:

    InquiryData -  the InquiryDatadata from the device
    RevisionId - destination buffer
    Length - Length of the destination buffer

Return Value:

    None.

--*/
{
    RevisionId[0] = '\0';

    if (Length > 4) {

        CopyField( RevisionId,
                   InquiryData->ProductRevisionLevel,
                   4,
                   ' '
                   );

        NT_ASSERT(RevisionId[4] == '\0');
        RevisionId[4] = ' ';

        RemoveTrailingBlanks(RevisionId, 5);
    }

    return;
}

VOID
DeviceInitAtapiIds(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PINQUIRYDATA InquiryData
    )
{
    FormatAtapiVendorId(InquiryData,
                        ChannelExtension->DeviceExtension->DeviceParameters.VendorId,
                        sizeof(ChannelExtension->DeviceExtension->DeviceParameters.VendorId)
                        );

    FormatAtapiRevisionId(InquiryData,
                          ChannelExtension->DeviceExtension->DeviceParameters.RevisionId,
                          sizeof(ChannelExtension->DeviceExtension->DeviceParameters.RevisionId)
                          );
    return;
}

VOID
UpdateDeviceParameters(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension
    )
/*++

Routine Description:

    Central place to update device related information after device enumeration

    Assumption - IDENTIFY DATA has been retrieved from device

Arguments:

    ChannelExtension -

Return Value:

    None.

--*/
{
    PATA_DEVICE_PARAMETERS deviceParameters = &ChannelExtension->DeviceExtension->DeviceParameters;
    PIDENTIFY_DEVICE_DATA  identifyDeviceData = ChannelExtension->DeviceExtension->IdentifyDeviceData;
    //PINQUIRYDATA           inquiryData = (PINQUIRYDATA)ChannelExtension->DeviceExtension->InquiryData;

    //1. re-initialize device specific information to avoid the values being reused after device switched.
    ChannelExtension->StateFlags.NCQ_Activated = 0;
    ChannelExtension->StateFlags.NCQ_Succeeded = 0;
    ChannelExtension->StateFlags.NCQ_HybridInfo = 0;

    AhciZeroMemory((PCHAR)&ChannelExtension->DeviceExtension->SupportedGPLPages, sizeof(ATA_SUPPORTED_GPL_PAGES));

    if (IsAtapiDevice(deviceParameters)) {
        deviceParameters->MaximumLun = 0;
        // following two fields are not used for ATAPI device.
        //deviceParameters->ScsiDeviceType = inquiryData->DeviceType;
        //deviceParameters->StateFlags.RemovableMedia = inquiryData->RemovableMedia;

        deviceParameters->MaxDeviceQueueDepth = 1;
        deviceParameters->AddressTranslation = UnknownMode;

    } else {
        // this is an ATA device
        BOOLEAN isBigLba = FALSE;

        deviceParameters->MaximumLun = 0;
        deviceParameters->ScsiDeviceType = DIRECT_ACCESS_DEVICE;
        deviceParameters->StateFlags.RemovableMedia = identifyDeviceData->GeneralConfiguration.RemovableMedia;

        deviceParameters->MaxDeviceQueueDepth = min(ChannelExtension->MaxPortQueueDepth, (UCHAR)identifyDeviceData->QueueDepth);

        if (identifyDeviceData->CommandSetSupport.WriteFua && identifyDeviceData->CommandSetActive.WriteFua) {
          // FUA support
            deviceParameters->StateFlags.FuaSupported = 1;
        }

        // ATA/ATAPI Revision 6.0 or later trust the bit in word 49 in the identify data.
        // by spec this shall be set to 1
        NT_ASSERT(identifyDeviceData->Capabilities.LbaSupported == 0x1);

        // check if it supports 48 bit LBA
        if (identifyDeviceData->CommandSetSupport.BigLba && identifyDeviceData->CommandSetActive.BigLba) {
            LARGE_INTEGER tempLba;

            tempLba.LowPart = identifyDeviceData->Max48BitLBA[0];
            tempLba.HighPart = identifyDeviceData->Max48BitLBA[1];

            // Some disk drives seem to set the above bits but
            // fail to respond to 48 bit LBA commands. So enable
            // 48 bit lba only if it is absolutely necessary
            if (tempLba.QuadPart >= MAX_28BIT_LBA) {
                isBigLba = TRUE;
            }
        }

        if (isBigLba) {
            deviceParameters->AddressTranslation = Lba48BitMode;
        } else {
            deviceParameters->AddressTranslation = LbaMode;
        }

      //2.1 Negotiate NCQ features
        if (IsNCQSupported(ChannelExtension)) {
            if (!IsDumpMode(ChannelExtension->AdapterExtension)) {
                ChannelExtension->StateFlags.NCQ_Activated = 1;
                deviceParameters->AddressTranslation = Lba48BitMode;
                deviceParameters->StateFlags.FuaSupported = 1;
            } else if (IsDumpHiberMode(ChannelExtension->AdapterExtension) && IsDeviceHybridInfoEnabled(ChannelExtension)) {
                // allow NCQ and Hybrid Info conveyed by NCQ Write command during hibernation.
                ChannelExtension->StateFlags.NCQ_Activated = 1;
                ChannelExtension->StateFlags.NCQ_HybridInfo = 1;
                deviceParameters->AddressTranslation = Lba48BitMode;
                deviceParameters->StateFlags.FuaSupported = 1;
            }
        }
    }

    DeviceInitAtaIds(ChannelExtension, identifyDeviceData);

    SelectDeviceGeometry(ChannelExtension, deviceParameters, identifyDeviceData);

    return;
}


ULONG
IOCTLtoATA(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
/*
    Note: If there is a need to send a command to device,
          the translation routine shall set appropriate value to srbExtension->AtaFunction.
            For example: srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
          Not setting this field can cause the Srb being completed earlier than expected.
*/
{
    PSRB_IO_CONTROL srbControl;
    ULONG status;

    srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;
    status = STOR_STATUS_SUCCESS;

    switch (srbControl->ControlCode) {
        case IOCTL_SCSI_MINIPORT_SMART_VERSION:

            status = SmartVersion (ChannelExtension, Srb);
            break;

        case IOCTL_SCSI_MINIPORT_IDENTIFY:

            status = SmartIdentifyData (ChannelExtension, Srb);
            break;

        case IOCTL_SCSI_MINIPORT_READ_SMART_ATTRIBS:
        case IOCTL_SCSI_MINIPORT_READ_SMART_THRESHOLDS:
        case IOCTL_SCSI_MINIPORT_ENABLE_SMART:
        case IOCTL_SCSI_MINIPORT_DISABLE_SMART:
        case IOCTL_SCSI_MINIPORT_RETURN_STATUS:
        case IOCTL_SCSI_MINIPORT_ENABLE_DISABLE_AUTOSAVE:
        case IOCTL_SCSI_MINIPORT_SAVE_ATTRIBUTE_VALUES:
        case IOCTL_SCSI_MINIPORT_EXECUTE_OFFLINE_DIAGS:
        case IOCTL_SCSI_MINIPORT_ENABLE_DISABLE_AUTO_OFFLINE:
        case IOCTL_SCSI_MINIPORT_READ_SMART_LOG:
        case IOCTL_SCSI_MINIPORT_WRITE_SMART_LOG:

            status = SmartGeneric (ChannelExtension, Srb);
            break;

        case IOCTL_SCSI_MINIPORT_NVCACHE: {

            // general NVCACHE parameter validation
            PSRB_IO_CONTROL             srbControl;
            PNVCACHE_REQUEST_BLOCK      nvCacheRequest;

            srbControl = (PSRB_IO_CONTROL)Srb->DataBuffer;

            if ( Srb->DataTransferLength < (sizeof(SRB_IO_CONTROL) + RTL_SIZEOF_THROUGH_FIELD(NVCACHE_REQUEST_BLOCK, NRBStatus)) ) {
                Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
                status = STOR_STATUS_BUFFER_TOO_SMALL;
                break;
            }

            nvCacheRequest = (PNVCACHE_REQUEST_BLOCK)(srbControl + 1);

            if ( (Srb->DataTransferLength < (sizeof(SRB_IO_CONTROL) + sizeof(NVCACHE_REQUEST_BLOCK) + nvCacheRequest->DataBufSize)) ||
                 (nvCacheRequest->DataBufSize > AHCI_MAX_TRANSFER_LENGTH) ) {

                nvCacheRequest->NRBStatus = NRB_INVALID_PARAMETER;
                Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
                status = STOR_STATUS_INVALID_PARAMETER;
                break;
            }

            // process NVCACHE request according to function
            switch (nvCacheRequest->Function) {

            case NRB_FUNCTION_PASS_HINT_PAYLOAD:
                status = NVCachePassHintPayload (ChannelExtension, Srb);
                break;

            default:
                status = NVCacheGeneric (ChannelExtension, Srb);
                break;

            }

            break;
        }



        default:

            Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
            status = STOR_STATUS_INVALID_PARAMETER;
            break;
    }

    return status;
}

ULONG
SmartVersion(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PGETVERSIONINPARAMS versionParameters;

    if (sizeof(SRB_IO_CONTROL) + sizeof(GETVERSIONINPARAMS) > Srb->DataTransferLength)
    {
        NT_ASSERT(FALSE);
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_BUFFER_TOO_SMALL;
    }

    versionParameters = (PGETVERSIONINPARAMS)(((PUCHAR)Srb->DataBuffer) +
                                              sizeof(SRB_IO_CONTROL));

    //
    // Version and revision per SMART 1.03
    //
    versionParameters->bVersion = 1;
    versionParameters->bRevision = 1;
    versionParameters->bReserved = 0;

    //
    // Indicate that support for IDE IDENTIFY, ATAPI IDENTIFY and SMART commands.
    //
    versionParameters->fCapabilities = (CAP_ATA_ID_CMD |
                                        CAP_ATAPI_ID_CMD |
                                        CAP_SMART_CMD);

    //
    // the bIDEDeviceMap is a bit map, with the bits defined as follows
    // bit 0 - IDE drive as device0 on Primary channel
    // bit 1 - IDE drive as device1 on Primary channel
    // bit 2 - IDE drive as device0 on Secondary channel
    // bit 3 - IDE drive as device1 on Secondary Channel
    // bit 4 - ATAPI drive as device0 on Primary Channel
    // bit 5 - ATAPI drive as device1 on Primary Channel
    // bit 6 - ATAPI drive as device0 on secondary Channel
    // bit 7 - ATAPI drive as device1 on secondary Channel
    //
    // since this doesn't apply to SATA, we can only fill in the fields pertinent to this channel.
    //

    if (IsAtapiDevice(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        versionParameters->bIDEDeviceMap = (1 << 4);
    } else {
        versionParameters->bIDEDeviceMap = (1 << 0);
    }

    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STOR_STATUS_SUCCESS;
}

BOOLEAN
FillClippedSGL(
    _In_    PSTOR_SCATTER_GATHER_LIST SourceSgl,
    _Inout_ PSTOR_SCATTER_GATHER_LIST LocalSgl,
    _In_    ULONG BytesLeft
    )
/*
    This routine cuts the beginning 'BytesLeft' from 'SourceSgl' and copy the left ranges to 'LocalSgl'.

        BytesLeft - the bytes count from starting of SourceSgl that should not be used.

    This routine is typically called by IOCTL to ATA command translaton, which the buffer contains some control information at the beginning.
    The real buffer for data transfer is following the controll information.
*/
{
    ULONG i, j, numElements;

    if ( (SourceSgl == NULL) || (LocalSgl == NULL) ) {
        return FALSE;
    }

    j = 0;
    numElements = SourceSgl->NumberOfElements;
    for (i = 0; i < SourceSgl->NumberOfElements; i++) {
        if (BytesLeft > 0 ) {
            if (BytesLeft < SourceSgl->List[i].Length) {
                //Done! Shrink this element
                LocalSgl->List[j].PhysicalAddress.LowPart = SourceSgl->List[i].PhysicalAddress.LowPart + BytesLeft;
                LocalSgl->List[j].PhysicalAddress.HighPart = SourceSgl->List[i].PhysicalAddress.HighPart;
                LocalSgl->List[j].Length = SourceSgl->List[i].Length - BytesLeft;
                BytesLeft = 0;
                j++;
            } else if (BytesLeft == SourceSgl->List[i].Length) {
                //Done! Cut off this element
                BytesLeft = 0;
                numElements--;
            } else {
                //notDone.  Cut off this element and shrink bytesLeft
                BytesLeft = BytesLeft - SourceSgl->List[i].Length;
                numElements--;
            }
        } else {
          //no modification necessary.  copy straight over.
            LocalSgl->List[j].PhysicalAddress.LowPart = SourceSgl->List[i].PhysicalAddress.LowPart;
            LocalSgl->List[j].PhysicalAddress.HighPart = SourceSgl->List[i].PhysicalAddress.HighPart;
            LocalSgl->List[j].Length = SourceSgl->List[i].Length;
            j++;
        }
    }

    NT_ASSERT(j == numElements);

    //record the number of elements left
    LocalSgl->NumberOfElements = numElements;


    if (numElements > 33) {
        return FALSE;
    } else {
        return TRUE;
    }
}


ULONG
SmartIdentifyData(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PSENDCMDOUTPARAMS outParams;
    PAHCI_SRB_EXTENSION         srbExtension;
    PUCHAR                      buffer;     //to make the pointer arithmatic easier

    buffer = (PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL);
    outParams = (PSENDCMDOUTPARAMS)buffer;

    if ( Srb->DataTransferLength < sizeof(SRB_IO_CONTROL) + sizeof(SENDCMDOUTPARAMS) ) {
        NT_ASSERT(FALSE);
        if ( Srb->DataTransferLength >= sizeof(SRB_IO_CONTROL) + RTL_SIZEOF_THROUGH_FIELD(SENDCMDOUTPARAMS, DriverStatus) ) {
            outParams->DriverStatus.bDriverError = SMART_INVALID_BUFFER;
            outParams->DriverStatus.bIDEError = 0;
        }
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_BUFFER_TOO_SMALL;
    }

    //ATAPI devices cannot support SMART (see ATA8-ACS2 r2 Section 7.19 Table 40 word 82 bit 0)
    if (IsAtapiDevice(&ChannelExtension->DeviceExtension->DeviceParameters)) {
        NT_ASSERT(FALSE);
        outParams->DriverStatus.bDriverError = SMART_INVALID_DRIVE;
        outParams->DriverStatus.bIDEError = 0;
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_DEVICE_REQUEST;
    }
  //1 Fills in the local SGL
    srbExtension = GetSrbExtension(Srb);
    srbExtension->AtaFunction = ATA_FUNCTION_ATA_IDENTIFY;
    srbExtension->Flags |= ATA_FLAGS_DATA_IN;
    srbExtension->CompletionRoutine = AhciPortSmartCompletion;

    //setup TaskFile
    AhciZeroMemory((PCHAR) &srbExtension->TaskFile, sizeof(ATA_TASK_FILE));
    srbExtension->TaskFile.Current.bDriveHeadReg = 0xA0;
    srbExtension->TaskFile.Current.bCommandReg = IDE_COMMAND_IDENTIFY;


    if (! FillClippedSGL(  StorPortGetScatterGatherList(ChannelExtension->AdapterExtension, Srb),
                    (PSTOR_SCATTER_GATHER_LIST) &srbExtension->LocalSgl,
                    sizeof(SRB_IO_CONTROL) + (sizeof(SENDCMDOUTPARAMS) - 1) ) ) {
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_BUFFER_TOO_SMALL;
    } else {
        srbExtension->Sgl = &srbExtension->LocalSgl;
        srbExtension->DataTransferLength = Srb->DataTransferLength - sizeof(SRB_IO_CONTROL) - (sizeof(SENDCMDOUTPARAMS) - 1);
    }

    Srb->SrbStatus = SRB_STATUS_PENDING;
    return STOR_STATUS_SUCCESS;
}

ULONG
SmartGeneric(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PSENDCMDOUTPARAMS           outParams;
    PSENDCMDINPARAMS            inParams;
    PAHCI_SRB_EXTENSION         srbExtension;
    PUCHAR                      buffer;//to make the pointer arithmatic easier

    buffer = (PUCHAR)Srb->DataBuffer + sizeof(SRB_IO_CONTROL);

    inParams  = (PSENDCMDINPARAMS ) buffer;
    outParams = (PSENDCMDOUTPARAMS) buffer;

    if (inParams->irDriveRegs.bCommandReg == SMART_CMD) {
        srbExtension = GetSrbExtension(Srb);

        switch (inParams->irDriveRegs.bFeaturesReg) {
            case READ_ATTRIBUTES:
            case READ_THRESHOLDS:
            case SMART_READ_LOG:
            case SMART_WRITE_LOG:
              //Setup the outbuffer to hold the data transfered
                //Ensure the PRDT will get set up
                if (inParams->irDriveRegs.bFeaturesReg == SMART_WRITE_LOG) {
                    srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
                } else {
                    srbExtension->Flags |= ATA_FLAGS_DATA_IN;
                }

                //Create the SGL to use to set up the PRDT
                if (! FillClippedSGL(  StorPortGetScatterGatherList(ChannelExtension->AdapterExtension, Srb),
                                (PSTOR_SCATTER_GATHER_LIST) &srbExtension->LocalSgl,
                                sizeof(SRB_IO_CONTROL) + (sizeof(SENDCMDOUTPARAMS) - 1) ) ) {
                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                    return STOR_STATUS_BUFFER_TOO_SMALL;
                } else {
                    srbExtension->Sgl = &srbExtension->LocalSgl;
                    srbExtension->DataTransferLength = Srb->DataTransferLength - sizeof(SRB_IO_CONTROL) - (sizeof(SENDCMDOUTPARAMS) - 1);
                }
                break;

            case RETURN_SMART_STATUS:
              //Setup outbuffer to recieve the return task file
                Srb->SenseInfoBuffer = outParams->bBuffer;
                Srb->SenseInfoBufferLength = 8;
                srbExtension->Flags |= ATA_FLAGS_RETURN_RESULTS;
              //there is no data transfer.
                break;

            case EXECUTE_OFFLINE_DIAGS:
              //Allow only the non-captive tests, for now.
                if ((inParams->irDriveRegs.bSectorNumberReg == SMART_SHORT_SELFTEST_CAPTIVE) ||
                    (inParams->irDriveRegs.bSectorNumberReg == SMART_EXTENDED_SELFTEST_CAPTIVE)) {

                    Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                    return STOR_STATUS_INVALID_DEVICE_REQUEST;
                }
              //there is no data transfer.
                //Nothing to do for these
                break;

            case ENABLE_SMART:
            case DISABLE_SMART:
            case SAVE_ATTRIBUTE_VALUES:
            case ENABLE_DISABLE_AUTOSAVE:
            case ENABLE_DISABLE_AUTO_OFFLINE:
              //there is no data transfer.
                //Nothing to do for these
                break;

            default:
                Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
                return STOR_STATUS_INVALID_DEVICE_REQUEST;
                break;

        }

    } else {
      //only smart commands are supported
        Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        return STOR_STATUS_INVALID_DEVICE_REQUEST;
    }

    //If we made it this far without changing the status, there is an ATA command to set up
    srbExtension->AtaFunction = ATA_FUNCTION_ATA_SMART;
    srbExtension->CompletionRoutine = AhciPortSmartCompletion;

    AhciZeroMemory((PCHAR) &srbExtension->TaskFile, sizeof(ATA_TASK_FILE));
    srbExtension->TaskFile.Current.bFeaturesReg = inParams->irDriveRegs.bFeaturesReg;
    srbExtension->TaskFile.Current.bSectorCountReg = inParams->irDriveRegs.bSectorCountReg;
    srbExtension->TaskFile.Current.bSectorNumberReg = inParams->irDriveRegs.bSectorNumberReg;
    srbExtension->TaskFile.Current.bCylLowReg = inParams->irDriveRegs.bCylLowReg;
    srbExtension->TaskFile.Current.bCylHighReg = inParams->irDriveRegs.bCylHighReg;
    srbExtension->TaskFile.Current.bDriveHeadReg = 0xA0 | (inParams->irDriveRegs.bDriveHeadReg & 0x0F);
    srbExtension->TaskFile.Current.bCommandReg = inParams->irDriveRegs.bCommandReg;

    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STOR_STATUS_SUCCESS;
}

ULONG
NVCacheGeneric(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PSRB_IO_CONTROL             srbControl;
    PNVCACHE_REQUEST_BLOCK      nRB;
    PAHCI_SRB_EXTENSION         srbExtension;
    ULONGLONG                   tempLBA;
    PNV_FEATURE_PARAMETER       idCacheData;

    ULONG                       status;

    srbExtension = GetSrbExtension(Srb);
    srbControl = (PSRB_IO_CONTROL) Srb->DataBuffer;
    nRB = ((PNVCACHE_REQUEST_BLOCK) ( (PSRB_IO_CONTROL)srbControl + 1) );

    switch (nRB->Function) {

//feature discovery
        case NRB_FUNCTION_NVCACHE_INFO:
          //Fill in the return _NV_FEATURE_PARAMETER and complete the command
            if ( Srb->DataTransferLength < (sizeof(SRB_IO_CONTROL) + sizeof(NVCACHE_REQUEST_BLOCK) + sizeof(NV_FEATURE_PARAMETER)) ) {
                if ( Srb->DataTransferLength >= (sizeof(SRB_IO_CONTROL) + RTL_SIZEOF_THROUGH_FIELD(NVCACHE_REQUEST_BLOCK, NRBStatus)) ) {
                    nRB->NRBStatus = NRB_INVALID_PARAMETER;
                }
                Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
                return STOR_STATUS_BUFFER_TOO_SMALL;
            }
            idCacheData = (PNV_FEATURE_PARAMETER) ( ( (PNVCACHE_REQUEST_BLOCK)nRB + 1) );
            idCacheData->NVPowerModeEnabled   = ChannelExtension->DeviceExtension->IdentifyDeviceData->NVCacheCapabilities.NVCachePowerModeEnabled;
            idCacheData->NVCmdEnabled         = ChannelExtension->DeviceExtension->IdentifyDeviceData->NVCacheCapabilities.NVCacheFeatureSetEnabled;
            idCacheData->NVPowerModeVer       = ChannelExtension->DeviceExtension->IdentifyDeviceData->NVCacheCapabilities.NVCachePowerModeVersion;
            idCacheData->NVCmdVer             = ChannelExtension->DeviceExtension->IdentifyDeviceData->NVCacheCapabilities.NVCacheFeatureSetVersion;
            idCacheData->NVSize               = ChannelExtension->DeviceExtension->IdentifyDeviceData->NVCacheSizeMSW;
            idCacheData->NVSize <<= 16;
            idCacheData->NVSize               += ChannelExtension->DeviceExtension->IdentifyDeviceData->NVCacheSizeLSW;
            idCacheData->NVReadSpeed          = 0;  // this field doesn't exist in ATA spec.
            idCacheData->NVWrtSpeed           = 0;  // this field doesn't exist in ATA spec.
            idCacheData->DeviceSpinUpTime     = ChannelExtension->DeviceExtension->IdentifyDeviceData->NVCacheOptions.NVCacheEstimatedTimeToSpinUpInSeconds;
            Srb->SrbStatus = SRB_STATUS_SUCCESS;
            nRB->NRBStatus = NRB_SUCCESS;
            return STOR_STATUS_SUCCESS;
            break;
//non data
        case NRB_FUNCTION_SPINDLE_STATUS:
        case NRB_FUNCTION_NVCACHE_POWER_MODE_SET:
        case NRB_FUNCTION_NVCACHE_POWER_MODE_RETURN:
            //Make sure this is not a data transfer.
            Srb->SrbFlags &= ~(SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT);
            srbExtension->Flags &= ~(SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT);

            break;

//data transfer
        case NRB_FUNCTION_QUERY_PINNED_SET:
        case NRB_FUNCTION_QUERY_CACHE_MISS:
        case NRB_FUNCTION_QUERY_HYBRID_DISK_STATUS:
        case NRB_FUNCTION_FLUSH_NVCACHE:
        case NRB_FUNCTION_ADD_LBAS_PINNED_SET:
        case NRB_FUNCTION_REMOVE_LBAS_PINNED_SET:
        case NRB_FUNCTION_QUERY_ASCENDER_STATUS:
          //Setup the outbuffer to hold the data transfered
            //Ensure the PRDT will get set up
            if( (nRB->Function == NRB_FUNCTION_QUERY_PINNED_SET) ||
                (nRB->Function == NRB_FUNCTION_QUERY_CACHE_MISS) ||
                (nRB->Function == NRB_FUNCTION_QUERY_HYBRID_DISK_STATUS) ||
                (nRB->Function == NRB_FUNCTION_QUERY_ASCENDER_STATUS)
                ) {
                srbExtension->Flags |= ATA_FLAGS_DATA_IN;
            } else {
                srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
            }

            if (! FillClippedSGL(StorPortGetScatterGatherList(ChannelExtension->AdapterExtension, Srb),
                            (PSTOR_SCATTER_GATHER_LIST) &srbExtension->LocalSgl,
                            sizeof(SRB_IO_CONTROL) + sizeof(NVCACHE_REQUEST_BLOCK) ) ) {
                if ( Srb->DataTransferLength >= (sizeof(SRB_IO_CONTROL) + RTL_SIZEOF_THROUGH_FIELD(NVCACHE_REQUEST_BLOCK, NRBStatus)) ) {
                    nRB->NRBStatus = NRB_INVALID_PARAMETER;
                }
                Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
                return STOR_STATUS_BUFFER_TOO_SMALL; //there is no TOO_BIG status
            } else {
                srbExtension->Sgl = &srbExtension->LocalSgl;
                srbExtension->DataTransferLength = Srb->DataTransferLength - sizeof(SRB_IO_CONTROL) - sizeof(NVCACHE_REQUEST_BLOCK);
            }
            break;

        default:
            if ( Srb->DataTransferLength >= (sizeof(SRB_IO_CONTROL) + RTL_SIZEOF_THROUGH_FIELD(NVCACHE_REQUEST_BLOCK, NRBStatus)) ) {
                nRB->NRBStatus = NRB_ILLEGAL_REQUEST;
            }

            Srb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
            return STOR_STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    //
    // Allocate and set SenseInfo buffer pointer - DMA friendly
    //
    Srb->SenseInfoBufferLength = sizeof (ATA_TASK_FILE);
    status = AhciAllocateDmaBuffer(ChannelExtension->AdapterExtension,
                                   Srb->SenseInfoBufferLength,
                                   (PVOID*)&Srb->SenseInfoBuffer);

    if ( (status != STOR_STATUS_SUCCESS) ||
         (Srb->SenseInfoBuffer == NULL) ) {
        //
        // Memory allocation failed
        //
        Srb->SrbStatus = SRB_STATUS_ERROR;
        return STOR_STATUS_INSUFFICIENT_RESOURCES;
    }

    AhciZeroMemory((PCHAR)Srb->SenseInfoBuffer, Srb->SenseInfoBufferLength);

    //
    // Make sure to recieve the return task file
    //
    srbExtension->Flags |= ATA_FLAGS_RETURN_RESULTS;


    srbExtension->AtaFunction = ATA_FUNCTION_ATA_COMMAND;
    srbExtension->CompletionRoutine = AhciPortNVCacheCompletion;

    AhciZeroMemory((PCHAR) &srbExtension->TaskFile, sizeof(ATA_TASK_FILE));

    srbExtension->TaskFile.Current.bFeaturesReg = (UCHAR) nRB->Function;

    srbExtension->TaskFile.Current.bSectorCountReg = (UCHAR) (0xFF & nRB->Count);
    srbExtension->TaskFile.Previous.bSectorCountReg =(UCHAR) (nRB->Count >> 8);

    tempLBA = nRB->LBA;
    srbExtension->TaskFile.Current.bSectorNumberReg =     (UCHAR) (0xFF & tempLBA);
    tempLBA >>= 8;
    srbExtension->TaskFile.Current.bCylLowReg =           (UCHAR) (0xFF & tempLBA);
    tempLBA >>= 8;
    srbExtension->TaskFile.Current.bCylHighReg =          (UCHAR) (0xFF & tempLBA);
    tempLBA >>= 8;
    srbExtension->TaskFile.Previous.bSectorNumberReg =    (UCHAR) (0xFF & tempLBA);
    tempLBA >>= 8;
    srbExtension->TaskFile.Previous.bCylLowReg =          (UCHAR) (0xFF & tempLBA);
    tempLBA >>= 8;
    srbExtension->TaskFile.Previous.bCylHighReg =         (UCHAR) (0xFF & tempLBA);

    srbExtension->TaskFile.Current.bDriveHeadReg = 0xA0;
    srbExtension->TaskFile.Current.bCommandReg = NVC_ATA_NV_CACHE_COMMAND;

    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    return  STOR_STATUS_SUCCESS;

}

ULONG
NVCachePassHintPayload(
    _In_ PAHCI_CHANNEL_EXTENSION ChannelExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb
    )
{
    PAHCI_SRB_EXTENSION     srbExtension;
    PSRB_IO_CONTROL         srbControl;
    PNVCACHE_REQUEST_BLOCK  nvCacheRequest;
    PNVCACHE_HINT_PAYLOAD   nvCacheHintPayload;

    UCHAR   command;
    UCHAR   subCommand;
    BOOLEAN commandSupported = FALSE;

    srbExtension = GetSrbExtension(Srb);
    srbControl = (PSRB_IO_CONTROL) Srb->DataBuffer;
    nvCacheRequest = (PNVCACHE_REQUEST_BLOCK)(srbControl + 1);
    nvCacheHintPayload = (PNVCACHE_HINT_PAYLOAD)(nvCacheRequest + 1);

    // 1.1 it has been validated in caller, data fields in nvCacheRequest are safe to access
    if (Srb->DataTransferLength < (sizeof(SRB_IO_CONTROL) + sizeof(NVCACHE_REQUEST_BLOCK) + sizeof(NVCACHE_HINT_PAYLOAD) + nvCacheRequest->Count * ATA_BLOCK_SIZE)) {
        if ( Srb->DataTransferLength >= (sizeof(SRB_IO_CONTROL) + RTL_SIZEOF_THROUGH_FIELD(NVCACHE_REQUEST_BLOCK, NRBStatus)) ) {
            nvCacheRequest->NRBStatus = NRB_INPUT_DATA_UNDERRUN;
        }
        // the associated buffer size is not big enough, use SRB_STATUS_BAD_SRB_BLOCK_LENGTH as SrbStatus.
        Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
        return STOR_STATUS_BUFFER_TOO_SMALL;
    }

    // limit the data buffer associated with NvCache request max to 8 blocks.
    if (nvCacheRequest->DataBufSize > (sizeof(NVCACHE_HINT_PAYLOAD) + 8 * ATA_BLOCK_SIZE)) {
        nvCacheRequest->NRBStatus = NRB_INVALID_PARAMETER;
        // the associated buffer size is too big, use SRB_STATUS_BAD_FUNCTION as SrbStatus.
        Srb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    command = nvCacheHintPayload->Command;

    if (command == IDE_COMMAND_NCQ_NON_DATA) {
        subCommand = (nvCacheHintPayload->Feature7_0 & 0x0F);      // subCommand is the lower 4 bits in Feature7_0 field.
        commandSupported = ( (subCommand == IDE_NCQ_NON_DATA_HYBRID_CHANGE_BY_SIZE) ||
                             (subCommand == IDE_NCQ_NON_DATA_HYBRID_CHANGE_BY_LBA_RANGE) ||
                             (subCommand == IDE_NCQ_NON_DATA_HYBRID_CONTROL) );
    } else if (command == IDE_COMMAND_SEND_FPDMA_QUEUED) {
        subCommand = (nvCacheHintPayload->Count15_8 & 0x1F);       // subCommand is the lower 5 bits in Count15_8 field.
        commandSupported = (subCommand == IDE_NCQ_SEND_HYBRID_EVICT);
    }

    // 1.2 if command not supported or Hybrid Information is not enabled, reject the request.
    if ((commandSupported == FALSE) || !IsDeviceHybridInfoEnabled(ChannelExtension) ) {
        nvCacheRequest->NRBStatus = NRB_ILLEGAL_REQUEST;
        Srb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    // 2.1 copy command fields into SrbExtension prior to calling IsNCQCommand()
    StorPortCopyMemory(&srbExtension->HintPayload, nvCacheHintPayload, sizeof(NVCACHE_HINT_PAYLOAD));

    // 2.2 reject the reject if NCQ command not supported or device doesn't work in NCQ mode
    if ( IsNCQCommand(srbExtension) &&
         (!IsNCQSupported(ChannelExtension) || (ChannelExtension->StateFlags.NCQ_Activated == 0)) ) {
        nvCacheRequest->NRBStatus = NRB_INVALID_PARAMETER;
        Srb->SrbStatus = SRB_STATUS_BAD_FUNCTION;
        return STOR_STATUS_INVALID_PARAMETER;
    }

    // 3 prepare SGL if needed
    if (nvCacheRequest->Count > 0) {
        // get SGL for the real data buffer that caller wants to transfer to device.
        // controlling data structures: SRB_IO_CONTROL, NVCACHE_REQUEST_BLOCK and NVCACHE_HINT_PAYLOAD should be skipped.
        if (!FillClippedSGL(StorPortGetScatterGatherList(ChannelExtension->AdapterExtension, Srb),
                            (PSTOR_SCATTER_GATHER_LIST) &srbExtension->LocalSgl,
                            sizeof(SRB_IO_CONTROL) + sizeof(NVCACHE_REQUEST_BLOCK) + sizeof(NVCACHE_HINT_PAYLOAD) ) ) {

            nvCacheRequest->NRBStatus = NRB_INVALID_PARAMETER;
            Srb->SrbStatus = SRB_STATUS_BAD_SRB_BLOCK_LENGTH;
            return STOR_STATUS_INVALID_PARAMETER;
        } else {
            srbExtension->Flags |= ATA_FLAGS_DATA_OUT;
            srbExtension->Sgl = &srbExtension->LocalSgl;
            srbExtension->DataTransferLength = nvCacheRequest->Count * ATA_BLOCK_SIZE;
        }
    }

    srbExtension->AtaFunction = ATA_FUNCTION_ATA_HINT_PAYLOAD;
    srbExtension->CompletionRoutine = AhciPortNVCacheCompletion;

    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STOR_STATUS_SUCCESS;
}


#if _MSC_VER >= 1200
#pragma warning(pop)
#else
#pragma warning(default:4214)
#pragma warning(default:4201)
#endif

