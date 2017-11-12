#include "photon/exc/Device.h"

#include "photon/core/Crc.h"
#include "photon/core/Endian.h"
#include "photon/core/Logging.h"
#include "photon/core/Generator.h"
#include "photon/core/Try.h"
#include "photon/exc/DataHeader.h"
#include "photon/exc/StreamDirection.h"
#include "photon/exc/StreamState.h"
#include "photon/exc/StreamHandler.h"
#include "photon/exc/ReceiptType.h"
#include "photon/exc/Utils.h"
#include "photon/pvu/Pvu.Component.h"
#include "photon/tm/Tm.Component.h"
#include "photon/fwt/Fwt.Component.h"
#include "photon/clk/Clk.Component.h"
#include "photon/exc/Exc.Constants.h"
#include "photon/exc/Exc.Component.h"

#include <string.h>

#define _PHOTON_FNAME "exc/Device.c"

static uint8_t inTemp[1024];

static void initStream(PhotonExcStreamState* self)
{
    self->currentReliableDownlinkCounter = UINT16_MAX / 2;
    self->currentReliableUplinkCounter = UINT16_MAX / 2;
    self->currentUnreliableDownlinkCounter = UINT16_MAX / 2;
    self->expectedReliableUplinkCounter = UINT16_MAX / 2;
    self->expectedUnreliableUplinkCounter = UINT16_MAX / 2;
}

static void init(PhotonExcDevice* self, uint64_t address)
{
    self->address = address;
    self->hasDataQueued = false;
    initStream(&self->cmdStream);
    initStream(&self->telemStream);
    initStream(&self->fwtStream);
    initStream(&self->userStream);
    PhotonRingBuf_Init(&self->inRingBuf, self->inRingBufData, sizeof(self->inRingBufData));
}

void PhotonExcDevice_InitGroundControl(PhotonExcDevice* self, uint64_t address)
{
    init(self, address);
    self->isGroundControl = true;
}

void PhotonExcDevice_InitUav(PhotonExcDevice* self, uint64_t address)
{
    init(self, address);
    self->isGroundControl = false;
}

#define HANDLE_INVALID_PACKET(self, ...)                      \
    do {                                                      \
        PHOTON_WARNING(__VA_ARGS__);                          \
        PHOTON_DEBUG("Continuing search with 1 byte offset"); \
        PhotonRingBuf_Erase(&self->inRingBuf, 1);             \
    } while(0);

static void handleJunk(PhotonExcDevice* self, const uint8_t* data, size_t size)
{
    (void)data;
    (void)self;
    if (size == 0) {
        return;
    }
    PHOTON_DEBUG("Recieved junk %zu bytes", size);
}

static inline uint8_t* findByte(uint8_t* begin, uint8_t* end, uint8_t value)
{
    while (begin < end) {
        if (*begin == value) return begin; begin++;
        if (*begin == value) return begin; begin++;
        if (*begin == value) return begin; begin++;
        if (*begin == value) return begin; begin++;
    }
    return end;
}

static bool findSep(PhotonExcDevice* self);
static bool findPacket(PhotonExcDevice* self, PhotonMemChunks* chunks);
static bool handlePacket(PhotonExcDevice* self, size_t size);
static PhotonError queueReceipt(PhotonExcDevice* self, const PhotonExcDataHeader* incomingHeader, void* data, PhotonGenerator gen);
static PhotonError genPayloadErrorReceiptPayload(void* data, PhotonWriter* dest);
static PhotonError genOkReceiptPayload(void* data, PhotonWriter* dest);
static PhotonError genCounterCorrectionReceiptPayload(void* data, PhotonWriter* dest);

void PhotonExcDevice_AcceptInput(PhotonExcDevice* self, const void* src, size_t size)
{
    //FIXME: return error if size > ringbuf size
    PhotonRingBuf_Write(&self->inRingBuf, src, size);

    bool canContinue;
    do {
        if (self->hasDataQueued) {
            return;
        }
        canContinue = findSep(self);
    } while (canContinue);
}

static bool findSep(PhotonExcDevice* self)
{
    const uint16_t separator = 0x9c3e;
    const uint8_t firstSepPart = (separator & 0xff00) >> 8;
    const uint8_t secondSepPart = separator & 0x00ff;
    PhotonMemChunks chunks = PhotonRingBuf_ReadableChunks(&self->inRingBuf);

    uint8_t* it = chunks.first.data;
    uint8_t* next;
    uint8_t* end = it + chunks.first.size;
    while (true) {
        it = findByte(it, end, firstSepPart);
        if (it == end) {
            break;
        }
        next = it + 1;
        if (next == end) {
            if (chunks.second.size != 0 && chunks.second.data[0] == secondSepPart) {
                size_t skippedSize = it - chunks.first.data;
                handleJunk(self, chunks.first.data, skippedSize);
                PhotonRingBuf_Erase(&self->inRingBuf, skippedSize);
                chunks.first.size = 0;
                chunks.second.data += 1;
                chunks.second.size -= 1;
                return findPacket(self, &chunks);
            }
            return false;
        }
        if (*next == secondSepPart) {
            size_t skippedSize = it - chunks.first.data;
            handleJunk(self, chunks.first.data, skippedSize);
            PhotonRingBuf_Erase(&self->inRingBuf, skippedSize);
            PHOTON_ASSERT(chunks.first.size >= (skippedSize + 2));
            chunks.first.data += skippedSize + 2;
            chunks.first.size -= skippedSize + 2;
            return findPacket(self, &chunks);
        }
        it++;
    }

    it = chunks.second.data;
    end = it + chunks.second.size;
    while (true) {
        it = findByte(it, end, firstSepPart);
        next = it + 1;
        if (next >= end) {
            handleJunk(self, chunks.first.data, chunks.first.size);
            handleJunk(self, chunks.second.data, chunks.second.size);
            PhotonRingBuf_Clear(&self->inRingBuf);
            return false;
        }
        if (*next == secondSepPart) {
            size_t skippedSize = it - chunks.second.data;
            handleJunk(self, chunks.first.data, chunks.first.size);
            handleJunk(self, chunks.second.data, skippedSize);
            PhotonRingBuf_Erase(&self->inRingBuf, chunks.first.size + skippedSize);
            chunks.first.size = 0;
            PHOTON_ASSERT(chunks.second.size >= (skippedSize + 2));
            chunks.second.data += skippedSize + 2;
            chunks.second.size -= skippedSize + 2;
            return findPacket(self, &chunks);
        }
        it++;
    }
}

static bool findPacket(PhotonExcDevice* self, PhotonMemChunks* chunks)
{
    size_t size = chunks->first.size + chunks->second.size;
    if (size < 2) {
        PHOTON_DEBUG("Packet size part not yet recieved");
        return false;
    }
    uint8_t firstSizePart;
    uint8_t secondSizePart;
    if (chunks->first.size >= 2) {
        firstSizePart = chunks->first.data[0];
        secondSizePart = chunks->first.data[1];
    } else if (chunks->first.size == 1) {
        firstSizePart = chunks->first.data[0];
        secondSizePart = chunks->second.data[0];
    } else {
        firstSizePart = chunks->second.data[0];
        secondSizePart = chunks->second.data[1];
    }

    size_t expectedSize = 2 + (firstSizePart | (secondSizePart << 8));

    size_t maxPacketSize = 1024; //TODO: define
    if (expectedSize > maxPacketSize) {
        HANDLE_INVALID_PACKET(self, "Recieved packet with size > max allowed"); //TODO: remove macro
        return true;
    }

    if (size < expectedSize) {
        //PHOTON_DEBUG("Packet payload not yet recieved");
        return false;
    }

    if (expectedSize <= chunks->first.size) {
        memcpy(inTemp, chunks->first.data, chunks->first.size);
    } else {
        memcpy(inTemp, chunks->first.data, chunks->first.size);
        memcpy(inTemp + chunks->first.size, chunks->second.data, size - expectedSize);
    }

    return handlePacket(self, expectedSize);
}

static bool handlePacket(PhotonExcDevice* self, size_t size)
{
    if (size < 4) {
        HANDLE_INVALID_PACKET(self, "Recieved packet with size < 4");
        return true;
    }
    uint16_t crc16 = Photon_Crc16(inTemp, size - 2);
    if (crc16 != Photon_Le16Dec(inTemp + size - 2)) {
        HANDLE_INVALID_PACKET(self, "Recieved packet with invalid crc");
        return true;
    }
    PhotonReader payload;
    PhotonReader_Init(&payload, inTemp + 2, size - 4);
    if(PhotonExcDataHeader_Deserialize(&self->incomingHeader, &payload) != PhotonError_Ok) {
        HANDLE_INVALID_PACKET(self, "Recieved packet with invalid header");
        return true;
    }
    if (self->incomingHeader.streamDirection != PhotonExcStreamDirection_Uplink) {
        HANDLE_INVALID_PACKET(self, "Recieved packet with invalid stream direction");
        return true;
    }
    if (self->incomingHeader.srcAddress != self->address) {
        HANDLE_INVALID_PACKET(self, "Recieved packet with invalid src address");
        return true;
    }
    if (self->incomingHeader.destAddress != PhotonExc_SelfAddress()) {
        HANDLE_INVALID_PACKET(self, "Recieved packet with invalid dest address");
        return true;
    }
    PhotonExcStreamState* state;
    PhotonExcStreamHandler handler;

    switch(self->incomingHeader.streamType) {
    case PhotonExcStreamType_Firmware: {
        if (!self->isGroundControl) {
            HANDLE_INVALID_PACKET(self, "Recieved fwt packet from uav");
            return true;
        }
        state = &self->fwtStream;
        handler = PhotonFwt_AcceptCmd;
        break;
    }
    case PhotonExcStreamType_Cmd: {
        state = &self->cmdStream;
        handler = PhotonPvu_ExecuteFrom;
        break;
    }
    case PhotonExcStreamType_Telem:
        HANDLE_INVALID_PACKET(self, "Telem packets not supported");
        return true;
    case PhotonExcStreamType_User:
        HANDLE_INVALID_PACKET(self, "User packets not supported");
        return true;
    }
    PhotonWriter results;
    PhotonWriter_Init(&results, self->outData, sizeof(self->outData));
    switch (self->incomingHeader.packetType) {
    case PhotonExcPacketType_Unreliable:
        //TODO: compare counters, check number of lost packets
        state->expectedUnreliableUplinkCounter = self->incomingHeader.counter + 1;
        if (handler(&self->incomingHeader, &payload, &results) != PhotonError_Ok) {
            HANDLE_INVALID_PACKET(self, "Recieved packet with invalid payload");
            return true;
        }
        break;
    case PhotonExcPacketType_Reliable:
        if (self->incomingHeader.counter != state->expectedReliableUplinkCounter) {
            queueReceipt(self, &self->incomingHeader, state, genCounterCorrectionReceiptPayload);
            HANDLE_INVALID_PACKET(self, "Invalid expected reliable counter");
            return false;
        }
        if (handler(&self->incomingHeader, &payload, &results) != PhotonError_Ok) {
            queueReceipt(self, &self->incomingHeader, 0, genPayloadErrorReceiptPayload);
            HANDLE_INVALID_PACKET(self, "Invalid payload");
            return false;
        }
        self->dataSize = results.current - results.start;
        PhotonError err = queueReceipt(self, &self->incomingHeader, self, genOkReceiptPayload);
        if (err != PhotonError_Ok) {
            PHOTON_CRITICAL("could not gen OK receipt");
        }
        state->expectedReliableUplinkCounter++;
        PhotonRingBuf_Erase(&self->inRingBuf, size + 2);
        return false;
    case PhotonExcPacketType_Receipt:
        HANDLE_INVALID_PACKET(self, "Uplink receipts not supported");
        break;
    }
    PhotonRingBuf_Erase(&self->inRingBuf, size + 2);
    return true;
}

static PhotonError genPayloadErrorReceiptPayload(void* data, PhotonWriter* dest)
{
    (void)data;
    PHOTON_TRY(PhotonExcReceiptType_Serialize(PhotonExcReceiptType_PayloadError, dest));
    return PhotonError_Ok;
}

static PhotonError genOkReceiptPayload(void* data, PhotonWriter* dest)
{
    PhotonExcDevice* self = (PhotonExcDevice*)data;
    PHOTON_TRY(PhotonExcReceiptType_Serialize(PhotonExcReceiptType_Ok, dest));
    size_t resultSize = self->dataSize;
    if (PhotonWriter_WritableSize(dest) < resultSize) {
        return PhotonError_NotEnoughSpace;
    }
    PhotonWriter_Write(dest, self->outData, self->dataSize);
    return PhotonError_Ok;
}

static PhotonError genCounterCorrectionReceiptPayload(void* data, PhotonWriter* dest)
{
    PHOTON_TRY(PhotonExcReceiptType_Serialize(PhotonExcReceiptType_CounterCorrection, dest));
    PhotonExcStreamState* state = (PhotonExcStreamState*)data;
    if (PhotonWriter_WritableSize(dest) < 2) {
        return PhotonError_NotEnoughSpace;
    }
    PhotonWriter_WriteU16Le(dest, state->expectedReliableUplinkCounter);
    return PhotonError_Ok;
}

static PhotonError queueReceipt(PhotonExcDevice* self, const PhotonExcDataHeader* incomingHeader, void* data, PhotonGenerator gen)
{
    self->request.data = data;
    self->request.gen = gen;
    self->request.header.streamDirection = PhotonExcStreamDirection_Downlink;
    self->request.header.packetType = PhotonExcPacketType_Receipt;
    self->request.header.streamType = incomingHeader->streamType;
    self->request.header.counter = incomingHeader->counter;
    self->request.header.srcAddress = incomingHeader->destAddress;
    self->request.header.destAddress = incomingHeader->srcAddress;

    self->hasDataQueued = true;
    return PhotonError_Ok;
}

static PhotonError genTm(void* data, PhotonWriter* dest)
{
    (void)data;
    return PhotonTm_CollectMessages(dest);
}

static PhotonError queueTmPacket(PhotonExcDevice* self)
{
    self->request.data = 0;
    self->request.gen = genTm;
    self->request.header.streamDirection = PhotonExcStreamDirection_Downlink;
    self->request.header.packetType = PhotonExcPacketType_Unreliable;
    self->request.header.streamType = PhotonExcStreamType_Telem;
    self->request.header.counter = self->telemStream.currentUnreliableDownlinkCounter;
    self->request.header.srcAddress = PhotonExc_SelfAddress();
    self->request.header.destAddress = self->address;

    self->telemStream.currentUnreliableDownlinkCounter++;
    self->hasDataQueued = true;
    return PhotonError_Ok;
}

static PhotonError genFwt(void* data, PhotonWriter* dest)
{
    (void)data;
    return PhotonFwt_GenAnswer(dest);
}

static PhotonError queueFwtPacket(PhotonExcDevice* self)
{
    self->request.data = 0;
    self->request.gen = genFwt;
    self->request.header.streamDirection = PhotonExcStreamDirection_Downlink;
    self->request.header.packetType = PhotonExcPacketType_Unreliable;
    self->request.header.streamType = PhotonExcStreamType_Firmware;
    self->request.header.counter = self->fwtStream.currentUnreliableDownlinkCounter;
    self->request.header.srcAddress = PhotonExc_SelfAddress();
    self->request.header.destAddress = self->address;

    self->fwtStream.currentUnreliableDownlinkCounter++;
    self->hasDataQueued = true;
    return PhotonError_Ok;
}

PhotonError PhotonDevice_QueueCustomCmdPacket(PhotonExcDevice* self, void* data, PhotonGenerator gen)
{
    self->request.data = data;
    self->request.gen = gen;
    self->request.header.streamDirection = PhotonExcStreamDirection_Uplink;
    self->request.header.packetType = PhotonExcPacketType_Unreliable; //TODO: make reliable
    self->request.header.streamType = PhotonExcStreamType_Cmd;
    self->request.header.counter = self->cmdStream.currentReliableUplinkCounter;
    self->request.header.srcAddress = PhotonExc_SelfAddress();
    self->request.header.destAddress = self->address;

    self->fwtStream.currentReliableUplinkCounter++;
    self->hasDataQueued = true;
    return PhotonError_Ok;
}

static PhotonError genPacket(PhotonExcDevice* self, PhotonWriter* dest)
{
    PhotonWriter_WriteU16Be(dest, PHOTON_EXC_STREAM_SEPARATOR);

    PHOTON_EXC_ENCODE_PACKET_HEADER(dest, reserved);

    self->request.header.time = PhotonClk_GetTime();

    PHOTON_TRY(PhotonExcDataHeader_Serialize(&self->request.header, &reserved));
    PHOTON_TRY(self->request.gen(self->request.data, &reserved));

    PHOTON_EXC_ENCODE_PACKET_FOOTER(dest, reserved);
    self->hasDataQueued = false;
    return PhotonError_Ok;
}

PhotonError PhotonExcDevice_GenNextPacket(PhotonExcDevice* self, PhotonWriter* dest)
{
    if (self->hasDataQueued) {
        return genPacket(self, dest);
    }
    if (self->isGroundControl) {
        if (PhotonFwt_HasAnswers()) {
            queueFwtPacket(self);
            return genPacket(self, dest);
        } else {
            queueTmPacket(self);
            return genPacket(self, dest);
        }
    }
    return PhotonError_NoDataAvailable;
}


#undef _PHOTON_FNAME
