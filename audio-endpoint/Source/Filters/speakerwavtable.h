/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    speakerwavtable.h

Abstract:

    Declaration of wave miniport tables for the render endpoints.
--*/

#ifndef _SIMPLEAUDIOSAMPLE_SPEAKERWAVTABLE_H_
#define _SIMPLEAUDIOSAMPLE_SPEAKERWAVTABLE_H_

#include "nativeldac_pcm_abi.h"
#ifdef NATIVE_LDAC_DIRECT_PDO_PROTOTYPE
#include "nativeldac_direct_pdo_public.h"
#endif

// Windows may select any LDAC-family sample rate at either 16 or 24 bits.

#define SPEAKER_DEVICE_MAX_CHANNELS                 2       // Max Channels.

#define SPEAKER_HOST_MAX_CHANNELS                   2       // Max Channels.
#define SPEAKER_HOST_MIN_BITS_PER_SAMPLE            16      // Min Bits Per Sample
#define SPEAKER_HOST_MAX_BITS_PER_SAMPLE            32      // Max container bits; 24-bit audio uses 24 valid bits in 32 bits
#define SPEAKER_HOST_MIN_SAMPLE_RATE                44100   // Min Sample Rate
#define SPEAKER_HOST_MAX_SAMPLE_RATE                96000   // Max Sample Rate

//
// Max # of pin instances.
//
#define SPEAKER_MAX_INPUT_SYSTEM_STREAMS            1

//=============================================================================

#define SPEAKER_PCM_CONTAINER_BITS(_bits)                             \
    ((_bits) == 24 ? 32 : (_bits))

#define SPEAKER_PCM_FORMAT(_rate, _bits)                              \
    {                                                                \
        {                                                            \
            sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),               \
            0,                                                       \
            0,                                                       \
            0,                                                       \
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),                   \
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),                  \
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)        \
        },                                                           \
        {                                                            \
            {                                                        \
                WAVE_FORMAT_EXTENSIBLE,                              \
                2,                                                   \
                (_rate),                                             \
                (_rate) * 2 * (SPEAKER_PCM_CONTAINER_BITS(_bits) / 8), \
                2 * (SPEAKER_PCM_CONTAINER_BITS(_bits) / 8),          \
                SPEAKER_PCM_CONTAINER_BITS(_bits),                   \
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)  \
            },                                                       \
            (_bits),                                                 \
            KSAUDIO_SPEAKER_STEREO,                                  \
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)                   \
        }                                                            \
    }

static 
KSDATAFORMAT_WAVEFORMATEXTENSIBLE SpeakerHostPinSupportedDeviceFormats[] =
{
    SPEAKER_PCM_FORMAT(48000, 16),
    SPEAKER_PCM_FORMAT(48000, 24),
    SPEAKER_PCM_FORMAT(44100, 16),
    SPEAKER_PCM_FORMAT(44100, 24),
    SPEAKER_PCM_FORMAT(88200, 16),
    SPEAKER_PCM_FORMAT(88200, 24),
    SPEAKER_PCM_FORMAT(96000, 16),
    SPEAKER_PCM_FORMAT(96000, 24),
};

#undef SPEAKER_PCM_FORMAT
#undef SPEAKER_PCM_CONTAINER_BITS

//
// Supported modes (only on streaming pins).
//
static
MODE_AND_DEFAULT_FORMAT SpeakerHostPinSupportedDeviceModes[] =
{
    {
        STATIC_AUDIO_SIGNALPROCESSINGMODE_DEFAULT,
        &SpeakerHostPinSupportedDeviceFormats[0].DataFormat  // 48 kHz, 16-bit
    }
};

//
// The entries here must follow the same order as the filter's pin
// descriptor array.
//
static 
PIN_DEVICE_FORMATS_AND_MODES SpeakerPinDeviceFormatsAndModes[] = 
{
    {
        SystemRenderPin,
        SpeakerHostPinSupportedDeviceFormats,
        SIZEOF_ARRAY(SpeakerHostPinSupportedDeviceFormats),
        SpeakerHostPinSupportedDeviceModes,
        SIZEOF_ARRAY(SpeakerHostPinSupportedDeviceModes)
    },
    {
        BridgePin,
        NULL,
        0,
        NULL,
        0
    }
};

//=============================================================================
static
KSDATARANGE_AUDIO SpeakerPinDataRangesStream[] =
{
    { // 0
        {
            sizeof(KSDATARANGE_AUDIO),
            KSDATARANGE_ATTRIBUTES,         // An attributes list follows this data range
            0,
            0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
        },
        SPEAKER_HOST_MAX_CHANNELS,           
        SPEAKER_HOST_MIN_BITS_PER_SAMPLE,    
        SPEAKER_HOST_MAX_BITS_PER_SAMPLE,    
        SPEAKER_HOST_MIN_SAMPLE_RATE,            
        SPEAKER_HOST_MAX_SAMPLE_RATE             
    }
};

static
PKSDATARANGE SpeakerPinDataRangePointersStream[] =
{
    PKSDATARANGE(&SpeakerPinDataRangesStream[0]),
    PKSDATARANGE(&PinDataRangeAttributeList),
};

//=============================================================================
static
KSDATARANGE SpeakerPinDataRangesBridge[] =
{
    {
        sizeof(KSDATARANGE),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
    }
};

static
PKSDATARANGE SpeakerPinDataRangePointersBridge[] =
{
    &SpeakerPinDataRangesBridge[0]
};

//=============================================================================
static
PCPIN_DESCRIPTOR SpeakerWaveMiniportPins[] =
{
    // Wave Out Streaming Pin (Renderer) KSPIN_WAVE_RENDER3_SINK_SYSTEM
    {
        SPEAKER_MAX_INPUT_SYSTEM_STREAMS,
        SPEAKER_MAX_INPUT_SYSTEM_STREAMS, 
        0,
        NULL,        // AutomationTable
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(SpeakerPinDataRangePointersStream),
            SpeakerPinDataRangePointersStream,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    // Wave Out Bridge Pin (Renderer) KSPIN_WAVE_RENDER3_SOURCE
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            SIZEOF_ARRAY(SpeakerPinDataRangePointersBridge),
            SpeakerPinDataRangePointersBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
};

//=============================================================================
//
//                   ----------------------------      
//                   |                          |      
//  Host Pin     0-->|                          |--> 1 KSPIN_WAVE_RENDER3_SOURCE
//                   |                          |      
//                   ----------------------------
static
PCCONNECTION_DESCRIPTOR SpeakerWaveMiniportConnections[] =
{
    { PCFILTER_NODE,            KSPIN_WAVE_RENDER3_SINK_SYSTEM,     PCFILTER_NODE,   KSPIN_WAVE_RENDER3_SOURCE }
};

//=============================================================================
static
PCPROPERTY_ITEM PropertiesSpeakerWaveFilter[] =
{
    {
        &KSPROPSETID_Pin,
        KSPROPERTY_PIN_PROPOSEDATAFORMAT,
        KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_Pin,
        KSPROPERTY_PIN_PROPOSEDATAFORMAT2,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacPcm,
        NativeLdacPcmPropertyInfo,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacPcm,
        NativeLdacPcmPropertyRead,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacPcm,
        NativeLdacPcmPropertyLinkState,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET |
            KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacPcm,
        NativeLdacPcmPropertyPreferredFormat,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET |
            KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacPcm,
        NativeLdacPcmPropertyPhysicalPresence,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET |
            KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacPcm,
        NativeLdacPcmPropertyConsumerLease,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET |
            KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    }
#ifdef NATIVE_LDAC_DIRECT_PDO_PROTOTYPE
    ,
    {
        &KSPROPSETID_NativeLdacDirectPdo,
        NldDirectPdoPropertySnapshot,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacDirectPdo,
        NldDirectPdoPropertyMediaStatus,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacDirectPdo,
        NldDirectPdoPropertyMediaPacket,
        KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    },
    {
        &KSPROPSETID_NativeLdacDirectPdo,
        NldDirectPdoPropertyRecovery,
        KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_WaveFilter
    }
#endif
};

static
PCEVENT_ITEM SpeakerWaveFilterEvents[] =
{
    {
        &KSEVENTSETID_PinCapsChange,
        KSEVENT_PINCAPS_FORMATCHANGE,
        KSEVENT_TYPE_ENABLE | KSEVENT_TYPE_BASICSUPPORT,
        CMiniportWaveRT_EventHandler_PinCapsChange
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP_EVENT(
    AutomationSpeakerWaveFilter,
    PropertiesSpeakerWaveFilter,
    SpeakerWaveFilterEvents);

//=============================================================================
static
PCFILTER_DESCRIPTOR SpeakerWaveMiniportFilterDescriptor =
{
    0,                                              // Version
    &AutomationSpeakerWaveFilter,                   // AutomationTable
    sizeof(PCPIN_DESCRIPTOR),                       // PinSize
    SIZEOF_ARRAY(SpeakerWaveMiniportPins),          // PinCount
    SpeakerWaveMiniportPins,                        // Pins
    sizeof(PCNODE_DESCRIPTOR),                      // NodeSize
    0,                                              // NodeCount
    NULL,                                           // Nodes
    SIZEOF_ARRAY(SpeakerWaveMiniportConnections),   // ConnectionCount
    SpeakerWaveMiniportConnections,                 // Connections
    0,                                              // CategoryCount
    NULL                                            // Categories  - use defaults (audio, render, capture)
};

#endif // _SIMPLEAUDIOSAMPLE_SPEAKERWAVTABLE_H_
