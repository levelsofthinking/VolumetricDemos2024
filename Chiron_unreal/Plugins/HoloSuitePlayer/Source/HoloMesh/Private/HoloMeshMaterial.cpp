// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshMaterial.h"

// - Construction -

UHoloMeshMaterial::UHoloMeshMaterial(const FObjectInitializer& ObjectInitializer)
{
}

UHoloMeshMaterial* UHoloMeshMaterial::Create(UMaterialInterface* ParentMaterial, UObject* InOuter)
{
	UObject* OuterObj = InOuter ? InOuter : (UObject*)GetTransientPackage();
	UHoloMeshMaterial* HMM = NewObject<UHoloMeshMaterial>(OuterObj);
	HMM->SetParent(ParentMaterial);
	HMM->CreateInstances();
	return HMM;
}

void UHoloMeshMaterial::SetParent(UMaterialInterface* NewParent)
{
	Parent = NewParent;
}

void UHoloMeshMaterial::CreateInstances()
{
	Material[ReadIndex] = UMaterialInstanceDynamic::Create(Parent, Outer);
	Material[WriteIndex] = UMaterialInstanceDynamic::Create(Parent, Outer);
}

// - Scalar Parameter -

void UHoloMeshMaterial::SetScalarParameterValueByName(FName ParameterName, float Value)
{
	Material[ReadIndex]->SetScalarParameterValue(ParameterName, Value);
	Material[WriteIndex]->SetScalarParameterValue(ParameterName, Value);
}

void UHoloMeshMaterial::SetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, float Value)
{
	Material[ReadIndex]->SetScalarParameterValueByInfo(ParameterInfo, Value);
	Material[WriteIndex]->SetScalarParameterValueByInfo(ParameterInfo, Value);
}

float UHoloMeshMaterial::GetScalarParameterValueByName(FName ParameterName)
{
	return Material[ReadIndex]->K2_GetScalarParameterValue(ParameterName);
}

float UHoloMeshMaterial::GetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo)
{
	return Material[ReadIndex]->K2_GetScalarParameterValueByInfo(ParameterInfo);
}

// - Vector Parameter -

void UHoloMeshMaterial::SetVectorParameterValueByName(FName ParameterName, FLinearColor Value)
{
	Material[ReadIndex]->SetVectorParameterValue(ParameterName, Value);
	Material[WriteIndex]->SetVectorParameterValue(ParameterName, Value);
}

void UHoloMeshMaterial::SetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, FLinearColor Value)
{
	Material[ReadIndex]->SetVectorParameterValueByInfo(ParameterInfo, Value);
	Material[WriteIndex]->SetVectorParameterValueByInfo(ParameterInfo, Value);
}

FLinearColor UHoloMeshMaterial::GetVectorParameterValueByName(FName ParameterName)
{
	return Material[ReadIndex]->K2_GetVectorParameterValue(ParameterName);
}

FLinearColor UHoloMeshMaterial::GetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo)
{
	return Material[ReadIndex]->K2_GetVectorParameterValueByInfo(ParameterInfo);
}

// - Texture Parameter -

void UHoloMeshMaterial::SetTextureParameterValueByName(FName ParameterName, UTexture* Value)
{
	Material[ReadIndex]->SetTextureParameterValue(ParameterName, Value);
	Material[WriteIndex]->SetTextureParameterValue(ParameterName, Value);
}

void UHoloMeshMaterial::SetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, UTexture* Value)
{
	Material[ReadIndex]->SetTextureParameterValueByInfo(ParameterInfo, Value);
	Material[WriteIndex]->SetTextureParameterValueByInfo(ParameterInfo, Value);
}

UTexture* UHoloMeshMaterial::GetTextureParameterValueByName(FName ParameterName)
{
	return Material[ReadIndex]->K2_GetTextureParameterValue(ParameterName);
}

UTexture* UHoloMeshMaterial::GetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo)
{
	return Material[ReadIndex]->K2_GetTextureParameterValueByInfo(ParameterInfo);
}
