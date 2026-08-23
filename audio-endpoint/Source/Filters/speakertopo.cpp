/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    speakertopo.cpp

Abstract:

    Implementation of topology miniport for the speaker (internal).
--*/

#pragma warning (disable : 4127)

#include "definitions.h"
#include "endpoints.h"
#include "mintopo.h"
#include "nativeldac_remote_container.h"
#include "speakertopo.h"
#include "speakertoptable.h"


#pragma code_seg("PAGE")
//=============================================================================
NTSTATUS
PropertyHandler_SpeakerTopoFilter
( 
    _In_ PPCPROPERTY_REQUEST      PropertyRequest 
)
/*++

Routine Description:

  Redirects property request to miniport object

Arguments:

  PropertyRequest - 

Return Value:

  NT status code.

--*/
{
    PAGED_CODE();

    ASSERT(PropertyRequest);

    DPF_ENTER(("[PropertyHandler_SpeakerTopoFilter]"));

    // PropertryRequest structure is filled by portcls. 
    // MajorTarget is a pointer to miniport object for miniports.
    //
    NTSTATUS            ntStatus = STATUS_INVALID_DEVICE_REQUEST;
    PCMiniportTopology  pMiniport = (PCMiniportTopology)PropertyRequest->MajorTarget;

    if (IsEqualGUIDAligned(*PropertyRequest->PropertyItem->Set, KSPROPSETID_Jack))
    {
        if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION)
        {
            ntStatus = pMiniport->PropertyHandlerJackDescription(
                PropertyRequest,
                ARRAYSIZE(SpeakerJackDescriptions),
                SpeakerJackDescriptions
                );
            if (NT_SUCCESS(ntStatus) &&
                (PropertyRequest->Verb & KSPROPERTY_TYPE_GET) != 0 &&
                PropertyRequest->ValueSize >=
                    sizeof(KSMULTIPLE_ITEM) + sizeof(KSJACK_DESCRIPTION))
            {
                PKSMULTIPLE_ITEM multipleItem =
                    (PKSMULTIPLE_ITEM)PropertyRequest->Value;
                PKSJACK_DESCRIPTION description =
                    (PKSJACK_DESCRIPTION)(multipleItem + 1);
                description->IsConnected =
                    pMiniport->NativeLdacIsJackConnected();
            }
        }
        else if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_DESCRIPTION2)
        {
            ntStatus = pMiniport->PropertyHandlerJackDescription2(
                PropertyRequest,
                ARRAYSIZE(SpeakerJackDescriptions),
                SpeakerJackDescriptions,
                JACKDESC2_PRESENCE_DETECT_CAPABILITY
                );
        }
        else if (PropertyRequest->PropertyItem->Id == KSPROPERTY_JACK_CONTAINERID)
        {
            ntStatus = pMiniport->PropertyHandlerJackContainerId(
                PropertyRequest,
                ARRAYSIZE(SpeakerJackDescriptions),
                SpeakerJackDescriptions,
                &NativeLdacRemoteContainerId
                );
        }
    }

    return ntStatus;
} // PropertyHandler_SpeakerTopoFilter

//=============================================================================
NTSTATUS
PropertyHandler_SpeakerTopology
(
    _In_ PPCPROPERTY_REQUEST      PropertyRequest
)
/*++

Routine Description:

  Redirects property request to miniport object

Arguments:

  PropertyRequest -

Return Value:

  NT status code.

--*/
{
    PAGED_CODE();

    ASSERT(PropertyRequest);

    DPF_ENTER(("[PropertyHandler_SpeakerTopology]"));

    // PropertryRequest structure is filled by portcls. 
    // MajorTarget is a pointer to miniport object for miniports.
    //
    PCMiniportTopology pMiniport = (PCMiniportTopology)PropertyRequest->MajorTarget;
    NTSTATUS ntStatus = pMiniport->PropertyHandlerGeneric(PropertyRequest);

    if (NT_SUCCESS(ntStatus) &&
        (PropertyRequest->Verb & KSPROPERTY_TYPE_SET) != 0 &&
        (PropertyRequest->PropertyItem->Id == KSPROPERTY_AUDIO_VOLUMELEVEL ||
         PropertyRequest->PropertyItem->Id == KSPROPERTY_AUDIO_MUTE))
    {
        pMiniport->GenerateControlChange(PropertyRequest->Node);
    }

    return ntStatus;
} // PropertyHandler_SpeakerTopology

//=============================================================================
#pragma code_seg()
NTSTATUS
EventHandler_SpeakerJackInfoChange
(
    _In_ PPCEVENT_REQUEST EventRequest
)
{
    PCMiniportTopology pMiniport =
        (PCMiniportTopology)EventRequest->MajorTarget;

    return pMiniport->EventHandlerJackInfoChange(EventRequest);
}

//=============================================================================
#pragma code_seg()
NTSTATUS
EventHandler_SpeakerControlChange
(
    _In_ PPCEVENT_REQUEST EventRequest
)
{
    PCMiniportTopology pMiniport =
        (PCMiniportTopology)EventRequest->MajorTarget;

    return pMiniport->EventHandlerControlChange(EventRequest);
}

#pragma code_seg()
