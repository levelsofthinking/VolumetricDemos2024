// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "Sequencer/AVVSectionEditor.h"
#include "Sequencer/AVVSection.h"
#include "SequencerSectionPainter.h"
#include "Fonts/FontCache.h"

FAVVSectionEditor::FAVVSectionEditor(UMovieSceneSection& InSection)
	: Section(Cast<UAVVSection>(&InSection))
{
}

UMovieSceneSection* FAVVSectionEditor::GetSectionObject()
{
	return Section;
}

int32 FAVVSectionEditor::OnPaintSection(FSequencerSectionPainter& InPainter) const
{
	// Draw Background
	const FColor HoloGreen(0, 151, 112);
	InPainter.LayerId = InPainter.PaintSectionBackground(HoloGreen);

	// Player not assigned draw no text.
	if (!Section->Player)
	{
		return InPainter.LayerId;
	}

	FString SectionTitle = "";

	// Get Section Title from Player
	{
		#if (ENGINE_MAJOR_VERSION >= 5)
			SectionTitle = Section->Player->GetActorNameOrLabel();
		#else
			const FString& ActorLabel = Section->Player->GetActorLabel();
			if (!ActorLabel.IsEmpty())
			{
				SectionTitle = ActorLabel;
			}
			else
			{
				SectionTitle = Section->Player->GetName();
			}
		#endif
	}

	// Setup Draw Area
	FSlateClippingZone ClippingZone(InPainter.SectionClippingRect.InsetBy(FMargin(1.0f)));
	FMargin ContentPadding = GetContentPadding();

	// Setup Font
#if (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1)
	FSlateFontInfo FontInfo = FAppStyle::GetFontStyle("NormalFont");
#else
	FSlateFontInfo FontInfo = FEditorStyle::GetFontStyle("NormalFont");
#endif

	// Vertical Centering is different between 4.x and 5.x
#if (ENGINE_MAJOR_VERSION >= 5)
	FVector2D TopLeft = InPainter.SectionGeometry.AbsoluteToLocal(InPainter.SectionClippingRect.GetTopLeft()) + FVector2D(1.f, -3.0f);
#else
	FVector2D TopLeft = InPainter.SectionGeometry.AbsoluteToLocal(InPainter.SectionClippingRect.GetTopLeft()) + FVector2D(1.f, -2.0f);
#endif
	
	TSharedRef<FSlateFontCache> FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();
	auto GetFontHeight = [&]
	{
		return FontCache->GetMaxCharacterHeight(FontInfo, 1.f) + FontCache->GetBaseline(FontInfo, 1.f);
	};
	while (GetFontHeight() > InPainter.SectionGeometry.Size.Y && FontInfo.Size > 11)
	{
		FontInfo.Size = FMath::Max(FMath::FloorToInt(FontInfo.Size - 6.f), 11);
	}

	// Draw Text
	InPainter.DrawElements.PushClip(ClippingZone);
	{
		FSlateDrawElement::MakeText(
			InPainter.DrawElements,
			InPainter.LayerId + 2,
			InPainter.SectionGeometry.MakeChild(
				FVector2D(InPainter.SectionGeometry.Size.X, GetFontHeight()),
				FSlateLayoutTransform(TopLeft + FVector2D(ContentPadding.Left, ContentPadding.Top))
			).ToPaintGeometry(), 
			SectionTitle,
			FontInfo,
			InPainter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
			FLinearColor::Black
		);
	}
	InPainter.DrawElements.PopClip();

	return InPainter.LayerId;
}

FText FAVVSectionEditor::GetSectionTitle() const
{
	return NSLOCTEXT("AVVSection", "AVVSectionLabel", "");
}