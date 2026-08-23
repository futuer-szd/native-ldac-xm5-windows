/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    minwavert.h

Abstract:

    Definition of wavert miniport class.
--*/

#ifndef _SIMPLEAUDIOSAMPLE_MINWAVERT_H_
#define _SIMPLEAUDIOSAMPLE_MINWAVERT_H_

#include "nativeldac_pcm_abi.h"
#include "nativeldac_pcm_owner_contract.h"
#include "nativeldac_presence_owner_contract.h"
#ifdef NATIVE_LDAC_DIRECT_PDO_PROTOTYPE
#include "nativeldac_direct_pdo_public.h"
#endif

//=============================================================================
// Referenced Forward
//=============================================================================
class CMiniportWaveRTStream;
typedef CMiniportWaveRTStream *PCMiniportWaveRTStream;

//=============================================================================
// Classes
//=============================================================================
///////////////////////////////////////////////////////////////////////////////
// CMiniportWaveRT
//   
class CMiniportWaveRT : 
    public IMiniportWaveRT,
    public IMiniportAudioSignalProcessing,
    public CUnknown
{
private:
    ULONG                               m_ulSystemAllocated;

    ULONG                               m_ulMaxSystemStreams;
    ULONG                               m_ulMaxOffloadStreams;
    ULONG                               m_ulMaxLoopbackStreams;

    // weak ref of running streams.
    PCMiniportWaveRTStream            * m_SystemStreams;

    BOOL                                m_bGfxEnabled;
    PBOOL                               m_pbMuted;
    PLONG                               m_plVolumeLevel;
    PKSDATAFORMAT_WAVEFORMATEXTENSIBLE  m_pMixFormat;
    PKSDATAFORMAT_WAVEFORMATEXTENSIBLE  m_pDeviceFormat;
    PCFILTER_DESCRIPTOR                 m_FilterDesc;
    PIN_DEVICE_FORMATS_AND_MODES *      m_DeviceFormatsAndModes;
    KSPIN_LOCK                          m_DeviceFormatsAndModesLock; // To serialize access.
    KIRQL                               m_DeviceFormatsAndModesIrql;
    ULONG                               m_DeviceFormatsAndModesCount; 
    USHORT                              m_DeviceMaxChannels;
    PDRMPORT                            m_pDrmPort;
    DRMRIGHTS                           m_MixDrmRights;
    ULONG                               m_ulMixDrmContentId;

    PUCHAR                              m_PcmRingBuffer;
    PUCHAR                              m_PcmReadScratchBuffer;
    ULONG                               m_PcmRingReadOffset;
    ULONG                               m_PcmRingWriteOffset;
    ULONG                               m_PcmRingAvailableBytes;
    ULONGLONG                           m_PcmStreamEpoch;
    ULONGLONG                           m_PcmTotalBytesWritten;
    ULONGLONG                           m_PcmTotalBytesRead;
    ULONGLONG                           m_PcmTotalBytesDropped;
    ULONG                               m_PcmSampleRate;
    USHORT                              m_PcmChannels;
    USHORT                              m_PcmBitsPerSample;
    ULONG                               m_PcmBlockAlign;
    BOOLEAN                             m_PcmStreamActive;
    KSPIN_LOCK                          m_PcmRingLock;
    NATIVE_LDAC_PCM_OWNER               m_PcmOwner;
    KSPIN_LOCK                          m_PcmOwnerLock;
    NATIVE_LDAC_PRESENCE_OWNER          m_PresenceOwner;
    KSPIN_LOCK                          m_PresenceOwnerLock;
    ULONG                               m_PreferredSampleRate;
    ULONG                               m_PreferredBitsPerSample;
    ULONG                               m_PreferredFormatRevision;

    union {
        PVOID                           m_DeviceContext;
    };

protected:
    PADAPTERCOMMON                      m_pAdapterCommon;
    ULONG                               m_DeviceFlags;
    eDeviceType                         m_DeviceType;
    PPORTEVENTS                         m_pPortEvents;
    PENDPOINT_MINIPAIR                  m_pMiniportPair;
    
public:
    NTSTATUS EventHandler_PinCapsChange
    (
        _In_  PPCEVENT_REQUEST EventRequest
    );

    NTSTATUS ValidateStreamCreate
    (
        _In_    ULONG   _Pin,
        _In_    BOOLEAN _Capture
    );
    
    NTSTATUS StreamCreated
    (
        _In_ ULONG                  _Pin,
        _In_ PCMiniportWaveRTStream _Stream
    );
    
    NTSTATUS StreamClosed
    (
        _In_ ULONG                  _Pin,
        _In_ PCMiniportWaveRTStream _Stream
    );
    
    NTSTATUS IsFormatSupported
    ( 
        _In_ ULONG          _ulPin,
        _In_ BOOLEAN        _bCapture,
        _In_ PKSDATAFORMAT  _pDataFormat
    );

    static NTSTATUS GetAttributesFromAttributeList
    (
        _In_ const KSMULTIPLE_ITEM *_pAttributes,
        _In_ size_t _Size,
        _Out_ GUID* _pSignalProcessingMode
    );

protected:
    NTSTATUS UpdateDrmRights
    (
        void
    );

public:
    DECLARE_STD_UNKNOWN();

#pragma code_seg("PAGE")
    CMiniportWaveRT(
        _In_            PUNKNOWN                                UnknownAdapter,
        _In_            PENDPOINT_MINIPAIR                      MiniportPair,
        _In_opt_        PVOID                                   DeviceContext
    )
        :CUnknown(0),
        m_ulMaxSystemStreams(0),
        m_PcmRingBuffer(NULL),
        m_PcmReadScratchBuffer(NULL),
        m_PcmRingReadOffset(0),
        m_PcmRingWriteOffset(0),
        m_PcmRingAvailableBytes(0),
        m_PcmStreamEpoch(0),
        m_PcmTotalBytesWritten(0),
        m_PcmTotalBytesRead(0),
        m_PcmTotalBytesDropped(0),
        m_PcmSampleRate(NATIVE_LDAC_PCM_DEFAULT_SAMPLE_RATE),
        m_PcmChannels(NATIVE_LDAC_PCM_DEFAULT_CHANNELS),
        m_PcmBitsPerSample(NATIVE_LDAC_PCM_DEFAULT_BITS_PER_SAMPLE),
        m_PcmBlockAlign(NATIVE_LDAC_PCM_DEFAULT_BLOCK_ALIGN),
        m_PcmStreamActive(FALSE),
        m_PcmRingLock(0),
        m_PreferredSampleRate(NATIVE_LDAC_PCM_DEFAULT_SAMPLE_RATE),
        m_PreferredBitsPerSample(NATIVE_LDAC_PCM_DEFAULT_BITS_PER_SAMPLE),
        m_PreferredFormatRevision(0),
        m_DeviceType(MiniportPair->DeviceType),
        m_DeviceContext(DeviceContext),
        m_DeviceMaxChannels(MiniportPair->DeviceMaxChannels),
        m_DeviceFormatsAndModes(MiniportPair->PinDeviceFormatsAndModes),
        m_DeviceFormatsAndModesCount(MiniportPair->PinDeviceFormatsAndModesCount),
        m_DeviceFlags(MiniportPair->DeviceFlags),
        m_pMiniportPair(MiniportPair)
    {
        PAGED_CODE();

        m_pAdapterCommon = (PADAPTERCOMMON)UnknownAdapter; // weak ref.

        if (MiniportPair->WaveDescriptor)
        {
            RtlCopyMemory(&m_FilterDesc, MiniportPair->WaveDescriptor, sizeof(m_FilterDesc));
            
            //
            // Get the max # of pin instances.
            //
            if (IsRenderDevice())
            {
                if (m_FilterDesc.PinCount > KSPIN_WAVE_RENDER2_SOURCE)
                {
                    m_ulMaxSystemStreams = m_FilterDesc.Pins[KSPIN_WAVE_RENDER2_SINK_SYSTEM].MaxFilterInstanceCount;
                    m_ulMaxLoopbackStreams = m_FilterDesc.Pins[KSPIN_WAVE_RENDER2_SINK_LOOPBACK].MaxFilterInstanceCount;
                }
                else if(m_FilterDesc.PinCount > KSPIN_WAVE_RENDER3_SOURCE)
                {
                    m_ulMaxSystemStreams = m_FilterDesc.Pins[KSPIN_WAVE_RENDER3_SINK_SYSTEM].MaxFilterInstanceCount;
                }
            }
            else
            {
                //
                // capture bridge pin comes first in the enumeration
                //
                if (m_FilterDesc.PinCount > KSPIN_WAVEIN_HOST)
                {
                    m_ulMaxSystemStreams = m_FilterDesc.Pins[KSPIN_WAVEIN_HOST].MaxFilterInstanceCount;
                }
            }
        }

        KeInitializeSpinLock(&m_DeviceFormatsAndModesLock);
        m_DeviceFormatsAndModesIrql = PASSIVE_LEVEL;
    }

#pragma code_seg()

    ~CMiniportWaveRT();

    IMP_IMiniportWaveRT;
    IMP_IMiniportAudioSignalProcessing;
    
    // Friends
    friend class        CMiniportWaveRTStream;
    friend class        CMiniportTopologySimpleAudioSample;
    
    friend NTSTATUS PropertyHandler_WaveFilter
    (   
        _In_ PPCPROPERTY_REQUEST      PropertyRequest 
    );   

public:
public:
    NTSTATUS PropertyHandlerProposedFormat
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerProposedFormat2

    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerPcmInfo
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerPcmRead
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerLinkState
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerPhysicalPresence
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerPcmConsumerLease
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerPreferredFormat
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

#ifdef NATIVE_LDAC_DIRECT_PDO_PROTOTYPE
    NTSTATUS PropertyHandlerDirectPdoSnapshot
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerDirectPdoMediaStatus
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerDirectPdoMediaPacket
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );

    NTSTATUS PropertyHandlerDirectPdoRecovery
    (
        _In_ PPCPROPERTY_REQUEST PropertyRequest
    );
#endif

    VOID BeginPcmStream(
        _In_ ULONG SampleRate,
        _In_ USHORT Channels,
        _In_ USHORT BitsPerSample,
        _In_ ULONG BlockAlign);
    VOID EndPcmStream();
    VOID PushPcm
    (
        _In_reads_bytes_(ByteCount) const BYTE* Data,
        _In_ ULONG ByteCount
    );

    PADAPTERCOMMON GetAdapterCommObj() 
    {
        return m_pAdapterCommon; 
    };
#pragma code_seg()

private:
    _IRQL_raises_(DISPATCH_LEVEL)
    _Acquires_lock_(m_DeviceFormatsAndModesLock)
    _Requires_lock_not_held_(m_DeviceFormatsAndModesLock)
    _IRQL_saves_global_(SpinLock, m_DeviceFormatsAndModesIrql)
    VOID AcquireFormatsAndModesLock();

    _Releases_lock_(m_DeviceFormatsAndModesLock)
    _Requires_lock_held_(m_DeviceFormatsAndModesLock)
    _IRQL_restores_global_(SpinLock, m_DeviceFormatsAndModesIrql)
    VOID ReleaseFormatsAndModesLock();

    _Post_satisfies_(return > 0)
    ULONG GetPinSupportedDeviceFormats(_In_ ULONG PinId, _Outptr_opt_result_buffer_(return) KSDATAFORMAT_WAVEFORMATEXTENSIBLE **ppFormats);

    _Success_(return != 0)
    ULONG GetPinSupportedDeviceModes(_In_ ULONG PinId, _Outptr_opt_result_buffer_(return) _On_failure_(_Deref_post_null_) MODE_AND_DEFAULT_FORMAT **ppModes);

#pragma code_seg()

protected:
#pragma code_seg()
    BOOL IsRenderDevice()
    {
        return m_DeviceType == eSpeakerDevice ? TRUE : FALSE;
    }

    BOOL IsSystemRenderPin(ULONG nPinId);

    BOOL IsSystemCapturePin(ULONG nPinId);

    BOOL IsBridgePin(ULONG nPinId);

    // These three pins are the pins used by the audio engine for host, loopback, and offload.
    ULONG GetSystemPinId()
    {
        ASSERT(IsRenderDevice());
        return KSPIN_WAVE_RENDER2_SINK_SYSTEM;
    }
};

typedef CMiniportWaveRT *PCMiniportWaveRT;

#endif // _SIMPLEAUDIOSAMPLE_MINWAVERT_H_
