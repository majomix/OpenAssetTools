#include "CommonLocalizeLoader.h"

#include "Localize/LocalizeCommon.h"
#include "Localize/Parsing/LocalizeFileReader.h"

#include <format>

CommonLocalizeLoader::CommonLocalizeLoader(ISearchPath& searchPath, Zone& zone)
    : m_search_path(searchPath),
      m_zone(zone)
{
}

std::string CommonLocalizeLoader::GetFileName(const std::string& assetName) const
{
    return std::format("{}/localizedstrings/{}.str", LocalizeCommon::GetNameOfLanguage(m_zone.m_language), assetName);
}

AssetCreationResult CommonLocalizeLoader::CreateLocalizeAsset(const std::string& assetName, AssetCreationContext& context)
{
    std::string fileName = GetFileName(assetName);

    const auto file = m_search_path.Open(fileName);
    if (!file.IsOpen())
        return AssetCreationResult::NoAction();

    LocalizeFileReader reader(*file.m_stream, assetName, m_zone.m_language, *this);

    std::vector<CommonLocalizeEntry> localizeEntries;
    if (!reader.ReadLocalizeFile(localizeEntries))
        return AssetCreationResult::Failure();

    AssetCreationResult lastResult = AssetCreationResult::NoAction();
    for (const auto& entry : localizeEntries)
    {
        lastResult = CreateAssetFromCommonAsset(entry, context);
        if (!lastResult.HasBeenSuccessful())
            return lastResult;
    }

    // File was found and parsed. If all entries were duplicates, localizeEntries is empty
    // but that's not an error — return last successful result or a dummy success.
    if (!lastResult.HasTakenAction())
    {
        // Create a dummy localize entry so the asset system sees a successful load
        return CreateAssetFromCommonAsset(CommonLocalizeEntry{assetName, ""}, context);
    }

    return lastResult;
}

bool CommonLocalizeLoader::CheckLocalizeEntryForDuplicates(const std::string& key)
{
    const auto existingEntry = m_keys.find(key);
    if (existingEntry != m_keys.end())
        return false;

    m_keys.emplace(key);
    return true;
}
