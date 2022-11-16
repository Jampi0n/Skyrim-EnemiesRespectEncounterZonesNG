#include "RelevelNpcs.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

#include "SimpleIni.h"

namespace EREZ {

    class Settings {
    public:
        [[nodiscard]] static Settings* GetSingleton() {
            static Settings singleton;
            return std::addressof(singleton);
        }

        int logLevel = 2;
        bool relevelUniques = true;
        bool relevelSummons = true;
        bool relevelFollowers = false;
        bool treatSummonsLikeOwner = true;
        bool includeLevelMult = true;
        bool extendLevels = false;
        int noZoneMin = 1;
        int noZoneMax = 1000;
        bool manualUninstall = false;

        void Load() {
            constexpr auto path = L"Data/SKSE/Plugins/EnemiesRespectEncounterZones.ini";

            CSimpleIniA ini;
            ini.SetUnicode();

            ini.LoadFile(path);

            getIni(ini, logLevel, "iLogLevel",
                   ";Controls how much information is logged. Level 0 has the most data, and level 6 turns logging "
                   "off. Default: 2");

            getIni(ini, relevelUniques, "bRelevelUniques",
                   ";Whether unique NPCs should be leveled according to the encounter zone. ");
            getIni(ini, relevelSummons, "bRelevelSummons",
                   ";Whether summons should be leveled according to the encounter zone. "
                   "As summons may move between encounter zones with the player, releveling should be "
                   "disabled to avoid them changing their power depending on where you are.");
            getIni(ini, relevelFollowers, "bRelevelFollowers",
                   ";Whether followers should be leveled according to the encounter zone. "
                   "As followers move between encounter zones, releveling should be disabled to avoid them "
                   "changing their power depending on where you are.");
            getIni(ini, treatSummonsLikeOwner, "bTreatSummonsLikeOwner",
                   ";If RelevelSummons is true, summons will be treated like their owner. If true, player "
                   "summons will not be releveled and summons from followers or unique npcs are treated "
                   "according to RelevelFollowers and RelevelUniques.");
            getIni(
                ini, includeLevelMult, "bIncludeLevelMult",
                ";Includes the player the player level multiplier in the encounter zone level caps. For example: An "
                "enemy may be set to be 2 times the player's level and the encounter zone may have a level range from "
                "10 - 30. With this setting enabled, the enemy will be releveled to 20 - 60.");
            getIni(
                ini, extendLevels, "bExtendLevels",
                ";NPC levels can be adjusted outside of their regular bounds. A NPC may be set to the level range "
                "15-50. With this setting enabled, it can get levels beyond that range. For instance if the encounter "
                "zone has a minimum level of 60, the NPC will also have a minimum level of 60.");
            getIni(ini, noZoneMin, "iNoZoneMin",
                   ";If the NPC is not inside an encounter zone, this is used as the minimum level.");
            getIni(ini, noZoneMax, "iNoZoneMax",
                   ";If the NPC is not inside an encounter zone, this is used as the maximum level.");

            getIni(ini, manualUninstall, "bManualUninstall",
                   ";To remove level changes from a save, set this to true. Load the save and make a new save. "
                   "Afterwards you can uninstall the mod. Already spawned npcs may keep their levels.");

            ini.SaveFile(path);

            auto log = spdlog::default_logger().get();
            auto newLogLevel = spdlog::level::level_enum::info;
            std::string newLogLevelName = "info";
            if (logLevel == 0) {
                newLogLevel = spdlog::level::level_enum::trace;
                newLogLevelName = "trace";
            } else if (logLevel == 1) {
                newLogLevel = spdlog::level::level_enum::debug;
                newLogLevelName = "debug";
            } else if (logLevel == 2) {
                newLogLevel = spdlog::level::level_enum::info;
                newLogLevelName = "info";
            } else if (logLevel == 3) {
                newLogLevel = spdlog::level::level_enum::warn;
                newLogLevelName = "warn";
            } else if (logLevel == 4) {
                newLogLevel = spdlog::level::level_enum::err;
                newLogLevelName = "err";
            } else if (logLevel == 5) {
                newLogLevel = spdlog::level::level_enum::critical;
                newLogLevelName = "critical";
            } else if (logLevel == 6) {
                newLogLevel = spdlog::level::level_enum::off;
                newLogLevelName = "off";
            }

            log->set_level(spdlog::level::level_enum::info);
            log->flush_on(spdlog::level::level_enum::info);
            logger::info("Setting log level to \"{}\".", newLogLevelName);
            log->set_level(newLogLevel);
            log->flush_on(newLogLevel);
        }

    private:
        Settings() { Load(); }

        static constexpr auto iniCategory = "General";

        static void getIni(CSimpleIniA& ini, bool& defaultValue, const char* settingName, const char* a_comment) {
            defaultValue = ini.GetBoolValue(iniCategory, settingName, defaultValue);
            ini.SetBoolValue(iniCategory, settingName, defaultValue, a_comment);
        }

        static void getIni(CSimpleIniA& ini, std::int32_t& defaultValue, const char* settingName,
                           const char* a_comment) {
            defaultValue = std::stoi(ini.GetValue(iniCategory, settingName, std::to_string(defaultValue).c_str()));
            ini.SetValue(iniCategory, settingName, std::to_string(defaultValue).c_str(), a_comment);
        }

        static void getIni(CSimpleIniA& ini, float& defaultValue, const char* settingName, const char* a_comment) {
            defaultValue = std::stof(ini.GetValue(iniCategory, settingName, std::to_string(defaultValue).c_str()));
            ini.SetValue(iniCategory, settingName, std::to_string(defaultValue).c_str(), a_comment);
        }
    };

    inline const auto Record_originalActorBaseLevels = _byteswap_ulong('TACT');

    class UnlevelManager {
    public:
        static UnlevelManager* GetSingleton() {
            static UnlevelManager singleton;
            return &singleton;
        }

        struct actorbaseData {
            uint16_t originalMin;
            uint16_t originalMax;
        };

        void OnPreLoad() {
            // When loading a save, reset all normal npc records
            // This happens before dynamic npc records are created, which are based on the normal ones and will now also
            // use the reset values
            ResetToOriginal();
        }

        void OnPostLoad() {
            // after the save is loaded, levels are also loaded and need to be reset when uninstalling
            // this only affects normal npcs, so dynamic npcs will keep their level until they respawn
            auto settings = Settings::GetSingleton();
            if (settings->manualUninstall) {
                ResetToOriginal();
                logger::info("Npc levels have been reset. Mod can be uninstalled now.");
            }
        }

        void OnDataInit() { ReadOriginalData(); }

    private:
        mutable std::mutex _lock;
        std::unordered_map<FormID, actorbaseData> originalActorBaseLevels;

        void ResetToOriginal() {
            logger::debug("resetting npc data...");
            int count = 0;
            int total = 0;
            const auto dataHandler = RE::TESDataHandler::GetSingleton();
            if (dataHandler) {
                for (const auto& npc : dataHandler->GetFormArray<RE::TESNPC>()) {
                    if (npc && npc->HasPCLevelMult()) {
                        uint16_t originalMin = 0;
                        uint16_t originalMax = 0;
                        auto baseFormID = npc->GetFormID();
                        if (originalActorBaseLevels.find(baseFormID) != originalActorBaseLevels.end()) {
                            auto& tmp = originalActorBaseLevels.at(baseFormID);
                            if (npc->actorData.calcLevelMin != tmp.originalMin ||
                                npc->actorData.calcLevelMax != tmp.originalMax) {
                                npc->actorData.calcLevelMin = tmp.originalMin;
                                npc->actorData.calcLevelMax = tmp.originalMax;
                                count++;
                            }
                            total++;
                        }
                    }
                }
            }
            logger::debug("reset npc data for {} of {} npcs", count, total);
        }

        void ReadOriginalData() {
            // Before any save is loaded all npc records are processed to store the original level values the original
            // values are required for the lower and upper bounds
            logger::debug("initializing npc data...");
            int count = 0;
            const auto dataHandler = RE::TESDataHandler::GetSingleton();
            if (dataHandler) {
                for (const auto& npc : dataHandler->GetFormArray<RE::TESNPC>()) {
                    if (npc && npc->HasPCLevelMult()) {
                        originalActorBaseLevels.insert_or_assign(
                            npc->GetFormID(), actorbaseData{npc->actorData.calcLevelMin, npc->actorData.calcLevelMax});
                        count++;
                    }
                }
            }
            logger::debug("initialized npc data for {} npcs", count);
        }

        bool Filter(Actor* actor, TESActorBase* base) {
            auto settings = Settings::GetSingleton();
            if (!settings->relevelUniques && (base->actorData.actorBaseFlags & ACTOR_BASE_DATA::Flag::kUnique)) {
                return false;
            }
            auto owner = actor->GetCommandingActor().get();
            // only treat summons that are their own forms (kSummonable) as summons
            // other summons are likely reanimated and should not be treated differently, otherwise regular NPCs of the
            // same form id will cause conflicts
            if (owner != NULL && base->actorData.actorBaseFlags & ACTOR_BASE_DATA::Flag::kSummonable) {
                if (!settings->relevelSummons) {
                    return false;
                }
                if (settings->treatSummonsLikeOwner) {
                    if (!Filter(owner, owner->GetActorBase())) {
                        return false;
                    }
                }
            }
            if (!settings->relevelFollowers && actor->IsPlayerTeammate()) {
                return false;
            }
            return true;
        }

        void LevelActorbase(TESActorBase* base, uint16_t min, uint16_t max) {
            auto baseFormID = base->GetFormID();
            float pcLevelMult = 0;
            uint16_t originalMin = 0;
            uint16_t originalMax = 0;
            uint32_t level = (uint32_t)base->actorData.level;

            // store original level data, if not yet stored
            // importantly, if it is not stored yet, the current values in the actorbase are still the original ones
            if (originalActorBaseLevels.find(baseFormID) == originalActorBaseLevels.end()) {
                originalMin = base->actorData.calcLevelMin;
                originalMax = base->actorData.calcLevelMax;
                pcLevelMult = level * 0.001f;
                originalActorBaseLevels.insert_or_assign(baseFormID, actorbaseData{originalMin, originalMax});
            }

            // change actorbase
            base->actorData.calcLevelMin = min;
            base->actorData.calcLevelMax = max;
        }

        void ResetActorbase(TESActorBase* base) {
            auto baseFormID = base->GetFormID();
            if (originalActorBaseLevels.find(baseFormID) != originalActorBaseLevels.end()) {
                auto& tmp = originalActorBaseLevels.at(baseFormID);
                if (base->actorData.calcLevelMin != tmp.originalMin ||
                    base->actorData.calcLevelMin != tmp.originalMin) {
                    logger::trace("resetting [{:X}]({}): {}-{}", baseFormID, base->GetName(), tmp.originalMin,
                                  tmp.originalMax);
                    base->actorData.calcLevelMin = tmp.originalMin;
                    base->actorData.calcLevelMax = tmp.originalMax;
                }

            } else {
            }
        }

        void RelevelActorbase(TESActorBase* base, BGSEncounterZone* EZ) {
            uint32_t level = (uint32_t)base->actorData.level;
            auto settings = Settings::GetSingleton();
            auto baseFormID = base->GetFormID();
            float pcLevelMult = level * 0.001f;
            uint16_t originalMin = 0;
            uint16_t originalMax = 0;

            // lookup original level data
            if (originalActorBaseLevels.find(baseFormID) == originalActorBaseLevels.end()) {
                originalMin = base->actorData.calcLevelMin;
                originalMax = base->actorData.calcLevelMax;
            } else {
                auto& tmp = originalActorBaseLevels.at(baseFormID);
                originalMin = tmp.originalMin;
                originalMax = tmp.originalMax;
            }

            logger::trace("relevel from [{:X}]({}): {}-{}", baseFormID, base->GetName(), originalMin, originalMax);

            // start with default min/max
            uint16_t minEZ = settings->noZoneMin;
            uint16_t maxEZ = settings->noZoneMax;

            // use encounter zone min/max, if valid
            if (EZ) {
                if (EZ->data.minLevel > 0) {
                    minEZ = EZ->data.minLevel;
                }
                if (EZ->data.maxLevel > 0) {
                    maxEZ = EZ->data.maxLevel;
                }
            }

            // use float for calculations
            float minTmp = minEZ;
            float maxTmp = maxEZ;

            // player mult
            if (settings->includeLevelMult) {
                minTmp *= pcLevelMult;
                maxTmp *= pcLevelMult;
            }

            // if extend is false, limit to original levels
            if (!settings->extendLevels) {
                minTmp = std::max(minTmp, originalMin * 1.0f);
                maxTmp = std::min(maxTmp, originalMax * 1.0f);
            }
            uint16_t minNew = (uint16_t)minTmp;
            uint16_t maxNew = (uint16_t)maxTmp;

            // limit to positive levels
            if (minNew <= 0) {
                minNew = 1;
            }
            if (maxNew <= 0) {
                maxNew = 1;
            }

            // so far nothing was changed
            // now perform relevel
            LevelActorbase(base, minNew, maxNew);
            logger::trace("to min={}, max={}", base->actorData.calcLevelMin, base->actorData.calcLevelMax);
        }

    public:
        void ProcessActor(Actor* actor) {
            auto base = actor->GetActorBase();
            if (!base->HasPCLevelMult()) {
                // only consider player-leveled npcs
                return;
            }
            auto settings = Settings::GetSingleton();

            if (!Filter(actor, base) || settings->manualUninstall) {
                // The actor might have been releveled earlier, because it changed follower state
                ResetActorbase(base);
                return;
            }

            // Only npcs that are in a loaded cell are relevant. Check if necessary cell data exists
            auto cell = actor->GetParentCell();
            if (!cell) {
                return;
            }
            auto loadedData = cell->GetRuntimeData().loadedData;
            if (!loadedData) {
                return;
            }

            // Use actor ref encounter zone
            // This is a hardcoded encounter zone for specific actor references and is used in exteriors, where not all
            // enemies in the current cell belong to the same EZ
            auto EZ = actor->extraList.GetEncounterZone();
            if (EZ == NULL) {
                // If no actor ref EZ exists, use cell EZ
                EZ = loadedData->encounterZone;
                if (EZ != NULL) {
                    logger::trace("using loc EZ");
                } else {
                    logger::trace("no EZ");
                }
            } else {
                logger::trace("using ref EZ");
            }
            // if cell EZ does not exist it is NULL, but that's ok
            RelevelActorbase(base, EZ);
        }

    private:
        UnlevelManager() = default;
        UnlevelManager(const UnlevelManager&) = delete;
        UnlevelManager(UnlevelManager&&) = delete;

        ~UnlevelManager() = default;

        UnlevelManager& operator=(const UnlevelManager&) = delete;
        UnlevelManager& operator=(UnlevelManager&&) = delete;
    };

    class OnActorLoadedEventHandler : public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
    public:
        static OnActorLoadedEventHandler* GetSingleton() {
            static OnActorLoadedEventHandler singleton;
            return &singleton;
        }

        static void RegisterListener() {
            RE::ScriptEventSourceHolder* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
            eventHolder->AddEventSink(OnActorLoadedEventHandler::GetSingleton());
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event,
                                              RE::BSTEventSource<RE::TESObjectLoadedEvent>* a_eventSource) override {
            if (a_event->loaded) {
                auto refID = a_event->formID;
                RE::TESForm* form = TESForm::LookupByID(refID);
                if (form) {
                    if (form->GetFormType() == FormType::ActorCharacter) {
                        auto ref = form->AsReference();
                        auto actor = static_cast<Actor*>(ref);
                        if (actor) {
                            UnlevelManager::GetSingleton()->ProcessActor(actor);
                        }
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        OnActorLoadedEventHandler() = default;
    };

    class OnScriptInitEventHandler : public RE::BSTEventSink<RE::TESInitScriptEvent> {
    public:
        static OnScriptInitEventHandler* GetSingleton() {
            static OnScriptInitEventHandler singleton;
            return &singleton;
        }

        static void RegisterListener() {
            RE::ScriptEventSourceHolder* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
            eventHolder->AddEventSink(OnScriptInitEventHandler::GetSingleton());
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESInitScriptEvent* a_event,
                                              RE::BSTEventSource<RE::TESInitScriptEvent>* a_eventSource) override {
            auto ref = a_event->objectInitialized.get();
            if (ref->GetFormType() == FormType::ActorCharacter) {
                auto actor = static_cast<Actor*>(ref);
                if (actor) {
                    UnlevelManager::GetSingleton()->ProcessActor(actor);
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        OnScriptInitEventHandler() = default;
    };

    class OnCellAttachEventHandler : public RE::BSTEventSink<RE::TESCellAttachDetachEvent> {
    public:
        static OnCellAttachEventHandler* GetSingleton() {
            static OnCellAttachEventHandler singleton;
            return &singleton;
        }

        static void RegisterListener() {
            RE::ScriptEventSourceHolder* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
            eventHolder->AddEventSink(OnCellAttachEventHandler::GetSingleton());
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCellAttachDetachEvent* a_event,
            RE::BSTEventSource<RE::TESCellAttachDetachEvent>* a_eventSource) override {
            if (a_event->attached) {
                auto ref = a_event->reference;
                if (ref->GetFormType() == FormType::ActorCharacter) {
                    auto actor = static_cast<Actor*>(ref);
                    if (actor) {
                        UnlevelManager::GetSingleton()->ProcessActor(actor);
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        OnCellAttachEventHandler() = default;
    };

    class OnMoveAttachEventHandler : public RE::BSTEventSink<RE::TESMoveAttachDetachEvent> {
    public:
        static OnMoveAttachEventHandler* GetSingleton() {
            static OnMoveAttachEventHandler singleton;
            return &singleton;
        }

        static void RegisterListener() {
            RE::ScriptEventSourceHolder* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
            eventHolder->AddEventSink(OnMoveAttachEventHandler::GetSingleton());
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESMoveAttachDetachEvent* a_event,
            RE::BSTEventSource<RE::TESMoveAttachDetachEvent>* a_eventSource) override {
            if (a_event->isCellAttached) {
                auto ref = a_event->movedRef.get();
                if (ref->GetFormType() == FormType::ActorCharacter) {
                    auto actor = static_cast<Actor*>(ref);
                    if (actor) {
                        UnlevelManager::GetSingleton()->ProcessActor(actor);
                    }
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        OnMoveAttachEventHandler() = default;
    };

    bool Init() {
        OnActorLoadedEventHandler::RegisterListener();
        OnScriptInitEventHandler::RegisterListener();
        OnCellAttachEventHandler::RegisterListener();
        OnMoveAttachEventHandler::RegisterListener();
        return true;
    }

    void OnDataInit() { UnlevelManager::GetSingleton()->OnDataInit(); }
    void OnPreLoad() { UnlevelManager::GetSingleton()->OnPreLoad(); }
    void OnPostLoad() { UnlevelManager::GetSingleton()->OnPostLoad(); }
}  // namespace EREZ
