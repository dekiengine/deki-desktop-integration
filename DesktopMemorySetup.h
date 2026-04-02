#pragma once

#include "SetupComponent.h"
#include "reflection/DekiProperty.h"
#include "DesktopHALModule.h"

/**
 * @brief Component to configure and initialize the desktop memory backend
 *
 * Add this component to your boot prefab to set up the desktop memory backend
 * (standard malloc/free, no external RAM).
 *
 * Inherits from SetupComponent to participate in boot sequence.
 */
class DEKI_DESKTOP_HAL_API DesktopMemorySetup : public SetupComponent
{
public:
    DEKI_COMPONENT(DesktopMemorySetup, SetupComponent, "Desktop HAL", "e5c0f3a2-7d9b-4e1c-8a63-9f2e1d6b4c80", "DEKI_FEATURE_DESKTOP_MEMORY_SETUP")

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "Desktop Memory"; }
};

// Generated property metadata
#include "generated/DesktopMemorySetup.gen.h"
