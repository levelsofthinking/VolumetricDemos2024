// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

// HoloSuite
#include "OMS/OMSUtilities.h"
#include "OMS/OMSPlayerComponent.h"
#include "RenderGraphUtils.h"

#include <string>

UTexture* OMSUtilities::GetMediaPlayerTexture(UMaterialInterface* sourceMaterial)
{
    UTexture* result = nullptr;

    TArrayView<UObject* const> MaterialTextures = sourceMaterial->GetReferencedTextures();
    if (MaterialTextures.Num() > 0)
    {
        result = Cast<UTexture>(MaterialTextures[0]);
    }
    else
    {
        TArray<UTexture*> Textures;
        sourceMaterial->GetUsedTextures(Textures, EMaterialQualityLevel::Num, true, ERHIFeatureLevel::Num, true);

        if (Textures.Num() > 0)
        {
            result = Textures[0];
        }
    }

    if (!result || result->GetFName() == TEXT("DefaultTexture"))
    {
        // Fallback for when it's one of our internally created media textures.
        sourceMaterial->GetTextureParameterValue(FName("BaseTexture"), result);
    }

    return result;
}

int OMSUtilities::DecodeBinaryPixels(unsigned char* pixelBlock)
{
    // Binary data is encoded in the image as 3 rows of 8 4x4 bits. Image data is returned
    // as a single long array, with every 4 elements being (RGBA) for a pixel. This translates
    // to 128 pixels per row and 16 pixels per bit. Each row is offset by 512 pixels. The loops
    // below parse that data.

    if (!pixelBlock)
    {
        return 0;
    }

    std::string rBinaryString = "";

    for (int i = 0; i < 128; i += 16)
    {
        if (static_cast<int>(pixelBlock[i]) > 128 && static_cast<int>(pixelBlock[i + 1]) > 128 && static_cast<int>(pixelBlock[i + 2]) > 128 &&
            static_cast<int>(pixelBlock[i + 4]) > 128 && static_cast<int>(pixelBlock[i + 5]) > 128 && static_cast<int>(pixelBlock[i + 6]) > 128)
        {
            rBinaryString += "1";
        }
        else
        {
            rBinaryString += "0";
        }
    }

    std::string gBinaryString = "";
    for (int i = 128; i < 256; i += 16)
    {
        if (static_cast<int>(pixelBlock[i]) > 128 && static_cast<int>(pixelBlock[i + 1]) > 128 && static_cast<int>(pixelBlock[i + 2]) > 128 &&
            static_cast<int>(pixelBlock[i + 4]) > 128 && static_cast<int>(pixelBlock[i + 5]) > 128 && static_cast<int>(pixelBlock[i + 6]) > 128)
        {
            gBinaryString += "1";
        }
        else
        {
            gBinaryString += "0";
        }
    }

    std::string bBinaryString = "";
    for (int i = 256; i < 256 + 128; i += 16)
    {
        if (static_cast<int>(pixelBlock[i]) > 128 && static_cast<int>(pixelBlock[i + 1]) > 128 && static_cast<int>(pixelBlock[i + 2]) > 128 &&
            static_cast<int>(pixelBlock[i + 4]) > 128 && static_cast<int>(pixelBlock[i + 5]) > 128 && static_cast<int>(pixelBlock[i + 6]) > 128)
        {
            bBinaryString += "1";
        }
        else
        {
            bBinaryString += "0";
        }
    }

    return std::stoi(rBinaryString, 0, 2) * 65536 + std::stoi(gBinaryString, 0, 2) * 256 + std::stoi(bBinaryString, 0, 2);
}

bool OMSUtilities::IsMobileHDREnabled()
{
    static auto* MobileHDRConsoleVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileHDR"));
    return MobileHDRConsoleVar->GetValueOnAnyThread() == 1;
}