#pragma once

using namespace RE;
using namespace SKSE;

namespace EREZ {
    bool Init();
    void OnGameLoaded(SKSE::SerializationInterface* serde);
    void OnGameSaved(SKSE::SerializationInterface* serde);
    void OnRevert(SKSE::SerializationInterface* serde);
    void PreSaveGame();
}  // namespace EREZ
