#ifndef _NATIVE_LDAC_PCM_ABI_H_
#define _NATIVE_LDAC_PCM_ABI_H_

// {4F79F8F5-5C69-4238-B227-4F525D1DCA67}
#define STATIC_KSPROPSETID_NativeLdacPcm                                  \
    0x4f79f8f5, 0x5c69, 0x4238, 0xb2, 0x27, 0x4f, 0x52, 0x5d, 0x1d, 0xca, 0x67
DEFINE_GUIDSTRUCT("4F79F8F5-5C69-4238-B227-4F525D1DCA67", KSPROPSETID_NativeLdacPcm);
#define KSPROPSETID_NativeLdacPcm DEFINE_GUIDNAMED(KSPROPSETID_NativeLdacPcm)

#define NATIVE_LDAC_PCM_ABI_VERSION              2u
#define NATIVE_LDAC_PCM_DEFAULT_SAMPLE_RATE      48000u
#define NATIVE_LDAC_PCM_DEFAULT_CHANNELS         2u
#define NATIVE_LDAC_PCM_DEFAULT_BITS_PER_SAMPLE  16u
#define NATIVE_LDAC_PCM_DEFAULT_BLOCK_ALIGN      4u
#define NATIVE_LDAC_PCM_RING_CAPACITY_BYTES      48000u
#define NATIVE_LDAC_PCM_MAX_READ_BYTES           4096u

#define NATIVE_LDAC_PCM_FLAG_STREAM_ACTIVE       0x00000001u
#define NATIVE_LDAC_PCM_FLAG_DISCONTINUITY        0x00000002u

typedef enum _NATIVE_LDAC_PCM_PROPERTY
{
    NativeLdacPcmPropertyInfo = 0,
    NativeLdacPcmPropertyRead = 1,
    NativeLdacPcmPropertyLinkState = 2,
    NativeLdacPcmPropertyPreferredFormat = 3,
    NativeLdacPcmPropertyPhysicalPresence = 4,
    NativeLdacPcmPropertyConsumerLease = 5
} NATIVE_LDAC_PCM_PROPERTY;

#define NATIVE_LDAC_PCM_CONSUMER_LEASE_ABI_VERSION 1u
#define NATIVE_LDAC_PCM_CONSUMER_LEASE_FLAG_NONE   0x00000000u

typedef enum _NATIVE_LDAC_PCM_CONSUMER_LEASE_STATE
{
    NativeLdacPcmConsumerReleased = 0,
    NativeLdacPcmConsumerAcquired = 1
} NATIVE_LDAC_PCM_CONSUMER_LEASE_STATE;

typedef struct _NATIVE_LDAC_PCM_CONSUMER_LEASE
{
    ULONG       Size;
    ULONG       AbiVersion;
    ULONG       State;
    ULONG       Flags;
    ULONGLONG   ConsumerGeneration;
} NATIVE_LDAC_PCM_CONSUMER_LEASE,
  *PNATIVE_LDAC_PCM_CONSUMER_LEASE;

#define NATIVE_LDAC_FORMAT_ABI_VERSION           1u
#define NATIVE_LDAC_FORMAT_FLAG_NONE              0x00000000u
#define NATIVE_LDAC_FORMAT_RATE_44100              0x00000001u
#define NATIVE_LDAC_FORMAT_RATE_48000              0x00000002u
#define NATIVE_LDAC_FORMAT_RATE_88200              0x00000004u
#define NATIVE_LDAC_FORMAT_RATE_96000              0x00000008u
#define NATIVE_LDAC_FORMAT_BITS_16                 0x00000001u
#define NATIVE_LDAC_FORMAT_BITS_24                 0x00000002u

typedef struct _NATIVE_LDAC_PREFERRED_FORMAT
{
    ULONG       Size;
    ULONG       AbiVersion;
    ULONG       SampleRate;
    ULONG       BitsPerSample;
    ULONG       Flags;
    ULONG       SupportedSampleRates;
    ULONG       SupportedBitsPerSample;
    ULONG       Revision;
} NATIVE_LDAC_PREFERRED_FORMAT, *PNATIVE_LDAC_PREFERRED_FORMAT;

#define NATIVE_LDAC_LINK_STATE_ABI_VERSION       1u
#define NATIVE_LDAC_LINK_STATE_FLAG_NONE         0x00000000u

typedef enum _NATIVE_LDAC_LINK_STATE_VALUE
{
    NativeLdacLinkStateDisconnected = 0,
    NativeLdacLinkStateConnecting = 1,
    NativeLdacLinkStateConnected = 2,
    NativeLdacLinkStateStopping = 3
} NATIVE_LDAC_LINK_STATE_VALUE;

typedef struct _NATIVE_LDAC_LINK_STATE
{
    ULONG       Size;
    ULONG       AbiVersion;
    ULONG       State;
    ULONG       Flags;
    ULONGLONG   SessionId;
    ULONGLONG   UpdateSequence;
    ULONGLONG   UpdatedInterruptTime100ns;
} NATIVE_LDAC_LINK_STATE, *PNATIVE_LDAC_LINK_STATE;

#define NATIVE_LDAC_PRESENCE_STATE_ABI_VERSION   1u
#define NATIVE_LDAC_PRESENCE_STATE_FLAG_NONE     0x00000000u

typedef enum _NATIVE_LDAC_PRESENCE_STATE_VALUE
{
    NativeLdacPresenceAbsent = 0,
    NativeLdacPresencePresent = 1
} NATIVE_LDAC_PRESENCE_STATE_VALUE;

typedef struct _NATIVE_LDAC_PRESENCE_STATE
{
    ULONG       Size;
    ULONG       AbiVersion;
    ULONG       State;
    ULONG       Flags;
    ULONGLONG   PresenceGeneration;
    ULONGLONG   UpdateSequence;
    ULONGLONG   UpdatedInterruptTime100ns;
} NATIVE_LDAC_PRESENCE_STATE, *PNATIVE_LDAC_PRESENCE_STATE;

typedef struct _NATIVE_LDAC_PCM_INFO
{
    ULONG       Size;
    ULONG       AbiVersion;
    ULONG       SampleRate;
    USHORT      Channels;
    USHORT      BitsPerSample;
    ULONG       BlockAlign;
    ULONG       CapacityBytes;
    ULONG       AvailableBytes;
    ULONG       Flags;
    ULONGLONG   StreamEpoch;
    ULONGLONG   TotalBytesWritten;
    ULONGLONG   TotalBytesRead;
    ULONGLONG   TotalBytesDropped;
} NATIVE_LDAC_PCM_INFO, *PNATIVE_LDAC_PCM_INFO;

typedef struct _NATIVE_LDAC_PCM_READ_HEADER
{
    NATIVE_LDAC_PCM_INFO InfoBeforeRead;
    ULONG                BytesReturned;
    ULONG                AvailableBytesAfterRead;
} NATIVE_LDAC_PCM_READ_HEADER, *PNATIVE_LDAC_PCM_READ_HEADER;

#endif // _NATIVE_LDAC_PCM_ABI_H_
