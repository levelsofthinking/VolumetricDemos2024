// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "HoloMeshMaterial.generated.h"

/**
 * UHoloMeshMaterial behaves like a UMaterialInstanceDynamic and is used for double buffering of HoloMeshes, allowing real-time manipulation of material properties on AVV player.
 */
UCLASS(hidecategories = Object, collapsecategories, BlueprintType)
class HOLOMESH_API UHoloMeshMaterial : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	// - Construction -

	static UHoloMeshMaterial* Create(UMaterialInterface* ParentMaterial, UObject* InOuter);
	void SetParent(UMaterialInterface* NewParent);
	void CreateInstances();

	// - Buffered Materials - 

	void Swap() { std::swap(ReadIndex, WriteIndex); }

	UMaterialInstanceDynamic* GetMaterial() { return Material[ReadIndex]; }
	UMaterialInstanceDynamic* GetMaterialByIndex(int index) { return Material[index]; }

	// - Scalar Parameter -

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "SetScalarParameterValue", ScriptName = "SetScalarParameterValue", Keywords = "SetFloatParameterValue"), Category = "AVV Player | Material")
	void SetScalarParameterValueByName(FName ParameterName, float Value);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "SetScalarParameterValueByInfo", ScriptName = "SetScalarParameterValueByInfo", Keywords = "SetFloatParameterValue"), Category = "AVV Player | Material")
	void SetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, float Value);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetScalarParameterValue", ScriptName = "GetScalarParameterValue", Keywords = "GetFloatParameterValue"), Category = "AVV Player | Material")
	float GetScalarParameterValueByName(FName ParameterName);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetScalarParameterValueByInfo", ScriptName = "GetScalarParameterValueByInfo", Keywords = "GetFloatParameterValue"), Category = "AVV Player | Material")
	float GetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo);

	// - Vector Parameter -

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "SetVectorParameterValue", ScriptName = "SetVectorParameterValue", Keywords = "SetColorParameterValue"), Category = "AVV Player | Material")
	void SetVectorParameterValueByName(FName ParameterName, FLinearColor Value);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "SetVectorParameterValueByInfo", ScriptName = "SetVectorParameterValueByInfo", Keywords = "SetColorParameterValue"), Category = "AVV Player | Material")
	void SetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, FLinearColor Value);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetVectorParameterValue", ScriptName = "GetVectorParameterValue", Keywords = "GetColorParameterValue"), Category = "AVV Player | Material")
	FLinearColor GetVectorParameterValueByName(FName ParameterName);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetVectorParameterValueByInfo", ScriptName = "GetVectorParameterValueByInfo", Keywords = "GetColorParameterValue"), Category = "AVV Player | Material")
	FLinearColor GetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo);

	// - Texture Parameter -

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "SetTextureParameterValue", ScriptName = "SetTextureParameterValue", Keywords = "SetTextureParameterValue"), Category = "AVV Player | Material")
	void SetTextureParameterValueByName(FName ParameterName, UTexture* Value);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "SetTextureParameterValueByInfo", ScriptName = "SetTextureParameterValueByInfo", Keywords = "SetTextureParameterValue"), Category = "AVV Player | Material")
	void SetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, UTexture* Value);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetTextureParameterValue", ScriptName = "GetTextureParameterValue", Keywords = "GetTextureParameterValue"), Category = "AVV Player | Material")
	UTexture* GetTextureParameterValueByName(FName ParameterName);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetTextureParameterValueByInfo", ScriptName = "GetTextureParameterValueByInfo", Keywords = "GetTextureParameterValue"), Category = "AVV Player | Material")
	UTexture* GetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo);

private:

	UMaterialInterface* Parent;
	UObject* Outer;
	FName Name;

	// Double buffered Material data
	UMaterialInstanceDynamic* Material[2];
	int ReadIndex = 0;
	int WriteIndex = 1;
};
