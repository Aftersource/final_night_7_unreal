#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

DEFINE_LOG_CATEGORY_STATIC(LogFN7Font, Log, All);

/**
 * The game's one font (Benito asked for one font used everywhere): Inknut Antiqua ExtraBold, a free Belwe-style
 * serif (SIL Open Font License, Content/Fonts/OFL.txt). At start every typeface of Slate's default font (Regular,
 * Bold, Light, Italic...) points at it, so every FCoreStyle::GetDefaultFontStyle / FN7Style font in the game uses it.
 * The .ttf ships loose (DirectoriesToAlwaysStageAsNonUFS = Fonts). Missing file = the engine font stays, nothing breaks.
 */
class FFinalNightModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		PostInitHandle = FCoreDelegates::OnPostEngineInit.AddStatic(&FFinalNightModule::UseGameFont);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostInitHandle);
		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	FDelegateHandle PostInitHandle;

	static void UseGameFont()
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Fonts/InknutAntiqua-ExtraBold.ttf")));
		if (!IFileManager::Get().FileExists(*Path))
		{
			UE_LOG(LogFN7Font, Warning, TEXT("FN7 font: %s not found, keeping the engine font"), *Path);
			return;
		}
		FCompositeFont& Composite = const_cast<FCompositeFont&>(FCoreStyle::GetDefaultFont().Get());
		int32 Swapped = 0;
		for (FTypefaceEntry& Entry : Composite.DefaultTypeface.Fonts)
		{
			Entry.Font = FFontData(Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			++Swapped;
		}
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
		{
			FSlateApplication::Get().GetRenderer()->FlushFontCache(TEXT("FN7 game font"));
		}
		UE_LOG(LogFN7Font, Display, TEXT("FN7 font: %d typefaces now use Inknut Antiqua ExtraBold"), Swapped);
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FFinalNightModule, FinalNight, "FinalNight");
