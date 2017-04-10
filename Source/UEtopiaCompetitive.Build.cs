// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UEtopiaCompetitive : ModuleRules
{
	public UEtopiaCompetitive(TargetInfo Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore","Json", "HeadMountedDisplay",
                "JsonUtilities",
                "HTTP",
                "OnlineSubsystem",
                "OnlineSubsystemUtils",
                "UMG"});
    }
}
