// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AVRCP_OBSERVER_IOCTL_H
#define NATIVE_LDAC_AVRCP_OBSERVER_IOCTL_H

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <Windows.h>
#include <winioctl.h>
#endif

#include <guiddef.h>

/* {59CFD04C-74A5-4D89-8BC5-2637B6D8A27A} */
DEFINE_GUID(GUID_DEVINTERFACE_NATIVE_LDAC_AVRCP_OBSERVER,
            0x59cfd04c, 0x74a5, 0x4d89,
            0x8b, 0xc5, 0x26, 0x37, 0xb6, 0xd8, 0xa2, 0x7a);

#define NLD_AVRCP_OBSERVER_ABI_MAJOR 0u
#define NLD_AVRCP_OBSERVER_ABI_MINOR 11u
#define FILE_DEVICE_NLD_AVRCP_OBSERVER 0x8001u

#define IOCTL_NLD_AVRCP_OBSERVER_GET_VERSION \
    CTL_CODE(FILE_DEVICE_NLD_AVRCP_OBSERVER, 0x800u, \
             METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_NLD_AVRCP_OBSERVER_GET_STATUS \
    CTL_CODE(FILE_DEVICE_NLD_AVRCP_OBSERVER, 0x801u, \
             METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_NLD_AVRCP_OBSERVER_DEQUEUE_EVENT \
    CTL_CODE(FILE_DEVICE_NLD_AVRCP_OBSERVER, 0x802u, \
             METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_NLD_AVRCP_OBSERVER_SEND_COMMAND \
    CTL_CODE(FILE_DEVICE_NLD_AVRCP_OBSERVER, 0x803u, \
             METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_NLD_AVRCP_OBSERVER_BEGIN_OBSERVATION \
    CTL_CODE(FILE_DEVICE_NLD_AVRCP_OBSERVER, 0x804u, \
             METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _NLD_AVRCP_OBSERVER_ABI_VERSION {
    ULONG Size;
    ULONG Major;
    ULONG Minor;
    ULONG Flags;
} NLD_AVRCP_OBSERVER_ABI_VERSION,
  *PNLD_AVRCP_OBSERVER_ABI_VERSION;

#define NLD_AVRCP_OBSERVER_STATUS_PROFILE_READY 0x00000001u
#define NLD_AVRCP_OBSERVER_STATUS_REMOTE_READY 0x00000002u
#define NLD_AVRCP_OBSERVER_STATUS_LOCAL_READY 0x00000004u
#define NLD_AVRCP_OBSERVER_STATUS_CHANNEL_OPEN 0x00000008u
#define NLD_AVRCP_OBSERVER_STATUS_VOLUME_SUPPORTED 0x00000010u
#define NLD_AVRCP_OBSERVER_STATUS_OBSERVING 0x00000020u
#define NLD_AVRCP_OBSERVER_STATUS_QUEUE_OVERFLOW 0x00000040u
#define NLD_AVRCP_OBSERVER_STATUS_OUTBOUND_OPEN 0x00000080u
#define NLD_AVRCP_OBSERVER_STATUS_OPEN_PENDING 0x00000100u
#define NLD_AVRCP_OBSERVER_STATUS_CHANNEL_HELD 0x00000200u
#define NLD_AVRCP_OBSERVER_STATUS_REMOTE_DISCONNECTED 0x00000400u
#define NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUIRED 0x00000800u
#define NLD_AVRCP_OBSERVER_STATUS_ACTIVATION_REQUESTED 0x00001000u

typedef struct _NLD_AVRCP_OBSERVER_WRITE_REQUEST {
    ULONG Size;
    ULONG PduId;
    ULONG Response;
    ULONG ParameterSize;
    UCHAR Parameters[8];
    ULONG Reserved[2];
} NLD_AVRCP_OBSERVER_WRITE_REQUEST,
  *PNLD_AVRCP_OBSERVER_WRITE_REQUEST;

typedef struct _NLD_AVRCP_OBSERVER_STATUS {
    ULONG Size;
    ULONG Flags;
    ULONGLONG AclGeneration;
    ULONG QueueDepth;
    ULONG DroppedEvents;
    LONG LastProtocolStatus;
    LONG LastOpenStatus;
    LONG LastCloseStatus;
    ULONG Reserved[5];
} NLD_AVRCP_OBSERVER_STATUS,
  *PNLD_AVRCP_OBSERVER_STATUS;

typedef enum _NLD_AVRCP_OBSERVER_EVENT_TYPE {
    NldAvrcpObserverEventNone = 0,
    NldAvrcpObserverEventAclConnected = 1,
    NldAvrcpObserverEventAclDisconnected = 2,
    NldAvrcpObserverEventVolumeCapability = 3,
    NldAvrcpObserverEventAbsoluteVolume = 4,
    NldAvrcpObserverEventPassThrough = 5,
    NldAvrcpObserverEventProtocolError = 6,
    NldAvrcpObserverEventVendorCommand = 7,
    NldAvrcpObserverEventWriteResponse = 8
} NLD_AVRCP_OBSERVER_EVENT_TYPE;

#define NLD_AVRCP_EVENT_FLAG_SUPPORTED 0x00000001u
#define NLD_AVRCP_EVENT_FLAG_RELEASED 0x00000002u
#define NLD_AVRCP_EVENT_FLAG_RESPONSE 0x00000004u
#define NLD_AVRCP_EVENT_FLAG_INTERIM 0x00000008u
#define NLD_AVRCP_EVENT_FLAG_CHANGED 0x00000010u
#define NLD_AVRCP_EVENT_FLAG_RAW_PREFIX 0x00000020u
#define NLD_AVRCP_EVENT_RAW_LENGTH_SHIFT 8u
#define NLD_AVRCP_EVENT_RAW_LENGTH_MASK 0x0000FF00u
#define NLD_AVRCP_EVENT_PACKET_SIZE_SHIFT 16u
#define NLD_AVRCP_EVENT_PACKET_SIZE_MASK 0x00FF0000u
#define NLD_AVRCP_EVENT_PARSE_STAGE_SHIFT 24u
#define NLD_AVRCP_EVENT_PARSE_STAGE_MASK 0xFF000000u

/*
 * NldAvrcpObserverEventWriteResponse carries the response to a locally
 * submitted SEND_COMMAND: Value0 = PDU id, ProtocolStatus = response code,
 * and RawPrefixHigh/RawPrefixHigh2 = the first eight parameter bytes.
 * For SetAbsoluteVolume responses, RawPrefixHigh & 0xFF is the accepted
 * absolute volume echoed by the peer.
 *
 * For NldAvrcpObserverEventProtocolError events, Flags encodes:
 *   RAW_PREFIX (bit 0x20) plus raw length in bits 8..15 (1..64),
 *   total packet size in bits 16..23 (capped at 255), and the parser
 *   failure stage in bits 24..31.  The failing packet bytes are carried
 *   in Value0 (0..3), RawPrefixHigh (4..7), RawPrefixHigh2 (8..11),
 *   RawPrefixHigh3 (12..15), RawPrefixHigh4 (16..19),
 *   RawPrefixHigh5 (20..23), RawPrefixHigh6 (24..27), and
 *   RawPrefixHigh7 (28..31), RawPrefixHigh8 (32..35),
 *   RawPrefixHigh9 (36..39), RawPrefixHigh10 (40..43),
 *   RawPrefixHigh11 (44..47), RawPrefixHigh12 (48..51),
 *   RawPrefixHigh13 (52..55), RawPrefixHigh14 (56..59), and
 *   RawPrefixHigh15 (60..63).
 */
typedef struct _NLD_AVRCP_OBSERVER_EVENT {
    ULONG Size;
    ULONG Type;
    ULONGLONG AclGeneration;
    ULONGLONG Sequence;
    ULONGLONG Timestamp100ns;
    ULONG Flags;
    ULONG Value0;
    LONG ProtocolStatus;
    ULONG RawPrefixHigh;
    ULONG RawPrefixHigh2;
    ULONG RawPrefixHigh3;
    ULONG RawPrefixHigh4;
    ULONG RawPrefixHigh5;
    ULONG RawPrefixHigh6;
    ULONG RawPrefixHigh7;
    ULONG RawPrefixHigh8;
    ULONG RawPrefixHigh9;
    ULONG RawPrefixHigh10;
    ULONG RawPrefixHigh11;
    ULONG RawPrefixHigh12;
    ULONG RawPrefixHigh13;
    ULONG RawPrefixHigh14;
    ULONG RawPrefixHigh15;
} NLD_AVRCP_OBSERVER_EVENT,
  *PNLD_AVRCP_OBSERVER_EVENT;

#endif
