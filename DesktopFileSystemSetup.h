#pragma once

#include <string>
#include "SetupComponent.h"
#include "reflection/DekiProperty.h"
#include "DesktopHALModule.h"

/**
 * @brief Component to configure and initialize the desktop file system
 *
 * Add this component to your boot prefab to set up the desktop file system
 * (standard C fopen/fread/fwrite).
 *
 * Inherits from SetupComponent to participate in boot sequence.
 */
class DEKI_DESKTOP_HAL_API DesktopFileSystemSetup : public SetupComponent
{
public:
    DEKI_COMPONENT(DesktopFileSystemSetup, SetupComponent, "Desktop HAL", "f6d1a4b3-8e0c-4f2d-9b74-0a3f2e7c5d91", "DEKI_FEATURE_DESKTOP_FILE_SYSTEM_SETUP")

    DEKI_EXPORT
    DEKI_TOOLTIP("Data directory path (maps to F:/)")
    std::string data_path = "./data/";

    DEKI_EXPORT
    DEKI_TOOLTIP("Storage directory path (maps to S:/)")
    std::string storage_path = "./storage/";

    void Setup(SetupCallback onComplete) override;
    const char* GetSetupName() const override { return "Desktop File System"; }
};

// Generated property metadata
#include "generated/DesktopFileSystemSetup.gen.h"
