// Copyright (C) Developed by Pask, Published by Dark Tower Interactive SRL 2024. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FAscentTargetingSystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
