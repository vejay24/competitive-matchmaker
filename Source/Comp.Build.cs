// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Comp : ModuleRules
{
	public Comp(TargetInfo Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay","Json",
                "JsonUtilities",
                "HTTP",
                "OnlineSubsystem",
                "OnlineSubsystemUtils",
                //"OnlineSubsystemUEtopia",
                "UMG" });

        //PublicIncludePaths.AddRange(new string[] { "OnlineSubsystemUEtopia/Public" });
    }
}
