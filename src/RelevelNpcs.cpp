#include "RelevelNpcs.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "SimpleIni.h"

RE::BGSEncounterZone* GetEncounterZone(RE::TESObjectREFR* This) {
    using func_t = decltype(&GetEncounterZone);
    REL::Relocation<func_t> func{RELOCATION_ID(19797, 20202)};
    return func(This);
}

namespace EREZ {

    class Helper {
    public:
        static std::string trim(const std::string& str, const std::string& whitespace = " \t") {
            const auto strBegin = str.find_first_not_of(whitespace);
            if (strBegin == std::string::npos) return "";  // no content

            const auto strEnd = str.find_last_not_of(whitespace);
            const auto strRange = strEnd - strBegin + 1;

            return str.substr(strBegin, strRange);
        }

        static std::unordered_set<std::string> splitString(const std::string& str, char sep) {
            std::string myStr = trim(str);
            std::stringstream test(myStr);
            std::string element;
            std::unordered_set<std::string> result;

            while (std::getline(test, element, sep)) {
                result.insert(element);
            }
            return result;
        }
    };

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
        bool noZoneSkip = true;

        bool pluginFilterInvert = false;
        std::string pluginFilterMaster = "";
        std::string pluginFilterAny = "";
        std::string pluginFilterWinning = "";

        bool forceAutoCalcAttributes = true;

        bool manualUninstall = false;

        std::unordered_set<std::string> pluginFilterMasterList;
        std::unordered_set<std::string> pluginFilterAnyList;
        std::unordered_set<std::string> pluginFilterWinningList;
        bool usePluginFilter = false;

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

            getIni(ini, noZoneSkip, "bNoZoneSkip", ";If the NPC is not inside an encounter zone, the NPC is skipped.");
            getIni(ini, noZoneMin, "iNoZoneMin",
                   ";If the NPC is not inside an encounter zone, this is used as the minimum level if bNoZoneSkip is "
                   "disabled.");
            getIni(ini, noZoneMax, "iNoZoneMax",
                   ";If the NPC is not inside an encounter zone, this is used as the maximum level if bNoZoneSkip is "
                   "disabled. Zero means infinite maximum level.");

            getIni(ini, pluginFilterInvert, "bPluginFilterInvert",
                   ";Inverts the plugin filter, meaning that ONLY filtered NPCs have their levels changed. By default, "
                   "all but the filtered NPCs have their levels changed.");
            getIni(ini, pluginFilterMaster, "sPluginFilterMaster",
                   ";NPC records that are master records in these plugins are filtered. Comma separated list, for "
                   "example: Skyrim.esm,Dawguard.esm");
            getIni(ini, pluginFilterAny, "sPluginFilterAny",
                   ";NPCs records that are changed in these plugins are filtered. Comma separated list, for example: "
                   "Skyrim.esm,Dawguard.esm");
            getIni(ini, pluginFilterWinning, "sPluginFilterWinning",
                   ";NPCs records that are winning records in these plugins are filtered. Comma separated list, for "
                   "example: Skyrim.esm,Dawguard.esm");

            getIni(ini, forceAutoCalcAttributes, "bForceAutoCalcAttributes",
                   ";Forces NPCs to recalculate their stats when the level is changed. This ensures attributes like "
                   "health are calculated using the new level.");

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

            pluginFilterMasterList = Helper::splitString(pluginFilterMaster, ',');
            pluginFilterAnyList = Helper::splitString(pluginFilterAny, ',');
            pluginFilterWinningList = Helper::splitString(pluginFilterWinning, ',');
            usePluginFilter =
                (pluginFilterMasterList.size() + pluginFilterAnyList.size() + pluginFilterWinningList.size()) > 0;
            logger::info("Use plugin filter: {}.", usePluginFilter);
            for (auto& str : pluginFilterMasterList) {
                logger::info("Filtering master npc records from {}.", str);
            }
            for (auto& str : pluginFilterAnyList) {
                logger::info("Filtering any npc records from {}.", str);
            }
            for (auto& str : pluginFilterWinningList) {
                logger::info("Filtering winning npc records from {}.", str);
            }
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

        static void getIni(CSimpleIniA& ini, std::string& defaultValue, const char* settingName,
                           const char* a_comment) {
            defaultValue = ini.GetValue(iniCategory, settingName, defaultValue.c_str());
            ini.SetValue(iniCategory, settingName, defaultValue.c_str(), a_comment);
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

        struct dynamicData {
            uint16_t originalMin;
            uint16_t originalMax;
            uint16_t modifiedMin;
            uint16_t modifiedMax;
            void* pointer;
        };

        void OnPreLoad() {
            // When loading a save, reset all normal npc records
            // This happens before dynamic npc records are created, which are based on the normal ones and will now also
            // use the reset values
            ResetToOriginal();
            // Reset all dynamic data, as dynamic FormIDs are recycled, so they may now refer to different objects
            dynamicActorBaseLevels.clear();
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
        std::unordered_map<FormID, dynamicData> dynamicActorBaseLevels;

        void SetActorBaseData(TESNPC* base, uint16_t originalMin, uint16_t originalMax, uint16_t min, uint16_t max) {
            auto baseFormID = base->GetFormID();
            if (baseFormID >= 0xff000000) {
                dynamicActorBaseLevels.insert_or_assign(baseFormID,
                                                        dynamicData{originalMin, originalMax, min, max, base});
            }
            base->actorData.calcLevelMin = min;
            base->actorData.calcLevelMax = max;
        }

        actorbaseData GetOriginalActorBaseData(TESNPC* base) {
            auto baseFormID = base->GetFormID();
            uint16_t originalMin = 0;
            uint16_t originalMax = 0;

            const void* address = static_cast<const void*>(base);
            std::stringstream ss;
            ss << address;
            auto str = ss.str();
            if (baseFormID >= 0xff000000) {
                bool dynamicDataIsValid = false;

                if (dynamicActorBaseLevels.find(baseFormID) != dynamicActorBaseLevels.end()) {
                    auto& tmp = dynamicActorBaseLevels.at(baseFormID);
                    if (tmp.modifiedMin == base->actorData.calcLevelMin &&
                        tmp.modifiedMax == base->actorData.calcLevelMax && tmp.pointer == base) {
                        dynamicDataIsValid = true;
                    } else {
                        dynamicActorBaseLevels.erase(baseFormID);
                    }
                }
                if (!dynamicDataIsValid) {
                    originalMin = base->actorData.calcLevelMin;
                    originalMax = base->actorData.calcLevelMax;
                } else {
                    auto& tmp = dynamicActorBaseLevels.at(baseFormID);
                    originalMin = tmp.originalMin;
                    originalMax = tmp.originalMax;
                }
            } else {
                if (originalActorBaseLevels.find(baseFormID) == originalActorBaseLevels.end()) {
                    originalMin = base->actorData.calcLevelMin;
                    originalMax = base->actorData.calcLevelMax;
                    originalActorBaseLevels.insert_or_assign(baseFormID, actorbaseData{originalMin, originalMax});
                } else {
                    auto& tmp = originalActorBaseLevels.at(baseFormID);
                    originalMin = tmp.originalMin;
                    originalMax = tmp.originalMax;
                }
            }
            return actorbaseData{originalMin, originalMax};
        }

        void ResetToOriginal() {
            logger::debug("Resetting npc data...");
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
            logger::debug("Reset npc data for {} of {} npcs.", count, total);
        }

        void ReadOriginalData() {
            // Before any save is loaded all npc records are processed to store the original level values the original
            // values are required for the lower and upper bounds
            logger::debug("Initializing npc data...");
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
            logger::debug("Initialized npc data for {} npcs.", count);
        }

        bool PluginFilter(Actor* actor, TESNPC* base) {
            auto settings = Settings::GetSingleton();
            if (!settings->usePluginFilter) {
                return true;
            }
            auto root = base->GetRootFaceNPC();
            if (root) {
                auto filesArray = root->sourceFiles.array;
                if (filesArray) {
                    auto first = 0;
                    auto last = filesArray->size() - 1;
                    std::string fileNameMaster = filesArray->data()[first]->fileName;
                    std::string fileNameWinning = filesArray->data()[last]->fileName;
                    if (!settings->pluginFilterInvert) {
                        if (settings->pluginFilterMasterList.find(fileNameMaster) !=
                            settings->pluginFilterMasterList.end()) {
                            return false;
                        }
                        if (settings->pluginFilterWinningList.find(fileNameWinning) !=
                            settings->pluginFilterWinningList.end()) {
                            return false;
                        }
                        if (settings->pluginFilterAnyList.size() > 0) {
                            for (int i = first; i <= last; i++) {
                                std::string fileNameAny = filesArray->data()[i]->fileName;
                                if (settings->pluginFilterAnyList.find(fileNameAny) !=
                                    settings->pluginFilterAnyList.end()) {
                                    return false;
                                }
                            }
                        }
                    } else {
                        bool include = false;
                        if (settings->pluginFilterMasterList.find(fileNameMaster) !=
                            settings->pluginFilterMasterList.end()) {
                            include = true;
                        } else {
                            if (settings->pluginFilterWinningList.find(fileNameWinning) !=
                                settings->pluginFilterWinningList.end()) {
                                include = true;
                            } else {
                                if (settings->pluginFilterAnyList.size() > 0) {
                                    for (int i = first; i <= last; i++) {
                                        std::string fileNameAny = filesArray->data()[i]->fileName;
                                        if (settings->pluginFilterAnyList.find(fileNameAny) !=
                                            settings->pluginFilterAnyList.end()) {
                                            include = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (!include) {
                            return false;
                        }
                    }

                } else {
                    logger::warn("Cannot find plugins referencing NPC. Plugin filter may not work as expected.");
                    logger::warn("NPC information: name = {}, ref id = {:X}, base id = {:X}, root id = {:X}",
                                 actor->GetName(), actor->GetFormID(), base->GetFormID(), root->GetFormID());
                }
            } else {
                logger::warn("Cannot find plugins referencing NPC. Plugin filter may not work as expected.");
                logger::warn("NPC information: name = {}, ref id = {:X}, base id = {:X}", actor->GetName(),
                             actor->GetFormID(), base->GetFormID());
            }
            return true;
        }

        bool StaticFilter(Actor* actor, TESNPC* base) {
            if (!base->HasPCLevelMult()) {
                // only consider player-leveled npcs
                return false;
            }
            return PluginFilter(actor, base);
        }

        bool Filter(Actor* actor, TESNPC* base) {
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

        void ResetActorbase(TESNPC* base) {
            auto baseFormID = base->GetFormID();
            if (originalActorBaseLevels.find(baseFormID) != originalActorBaseLevels.end()) {
                auto& tmp = originalActorBaseLevels.at(baseFormID);
                if (base->actorData.calcLevelMin != tmp.originalMin ||
                    base->actorData.calcLevelMin != tmp.originalMin) {
                    logger::trace("Resetting [{:X}]({}) to level range {}-{}.", baseFormID, base->GetName(),
                                  tmp.originalMin, tmp.originalMax);
                    base->actorData.calcLevelMin = tmp.originalMin;
                    base->actorData.calcLevelMax = tmp.originalMax;
                }
            }
        }

        void RelevelActorbase(TESNPC* base, uint16_t minLevel, uint16_t maxLevel) {
            if (minLevel > maxLevel && maxLevel != 0) {
                logger::warn("minLevel ({}) > maxLevel ({}), setting maxLevel to minLevel", minLevel, maxLevel);
                maxLevel = minLevel;
            }

            uint32_t level = (uint32_t)base->actorData.level;
            auto settings = Settings::GetSingleton();
            auto baseFormID = base->GetFormID();
            auto root = base->GetRootFaceNPC();
            if (root == NULL) {
                root = base;
            }
            float pcLevelMult = level * 0.001f;
            uint16_t originalMin = 0;
            uint16_t originalMax = 0;

            // lookup original level data
            auto original = GetOriginalActorBaseData(base);
            originalMin = original.originalMin;
            originalMax = original.originalMax;

            if (originalMin > originalMax && originalMax != 0) {
                logger::warn("originalMin ({}) > originalMax ({}), setting originalMax to originalMin", originalMin,
                             originalMax);
                originalMax = originalMin;
            }

            // use float for calculations
            float minTmp = minLevel;
            float maxTmp = maxLevel;

            // player mult
            float factor = 1.0;
            if (settings->includeLevelMult) {
                factor = pcLevelMult;
                minTmp *= factor;
                maxTmp *= factor;
            }

            if (!settings->extendLevels) {
                if (originalMax == 0) {
                    // original max level is unlimited -> only limit by originalMin
                    minTmp = std::max(minTmp, originalMin * 1.0f);
                    if (maxLevel == 0) {
                        // if maxLevel is 0, there will be no maximum level
                        maxTmp = 0;
                    } else {
                        maxTmp = std::max(maxTmp, originalMin * 1.0f);
                    }
                } else {
                    // limit minTmp to original level range
                    minTmp = std::min(std::max(minTmp, originalMin * 1.0f), originalMax * 1.0f);
                    if (maxLevel == 0) {
                        // if maxTmp == 0, max level is set as high as possible, which is originalMax
                        maxTmp = originalMax;
                    } else {
                        // limit maxTmp to original level range
                        maxTmp = std::min(std::max(maxTmp, originalMin * 1.0f), originalMax * 1.0f);
                    }
                }
            }
            uint16_t minNew = (uint16_t)minTmp;
            uint16_t maxNew = (uint16_t)maxTmp;

            // limit to positive levels
            if (minNew <= 0) {
                minNew = 1;
            }
            if (maxNew <= 0) {
                maxNew = 0;
            }

            // so far nothing was changed
            // now perform relevel
            SetActorBaseData(base, originalMin, originalMax, minNew, maxNew);

            logger::trace(
                "    Relevel base [{:X}/{:X}]({}) from level range {}-{} to level range {}-{} using factor {} .",
                baseFormID, root->GetFormID(), base->GetName(), originalMin, originalMax, base->actorData.calcLevelMin,
                base->actorData.calcLevelMax, factor);
        }

    public:
        void ProcessActor(Actor* actor) {
            auto base = actor->GetActorBase();
            if (!StaticFilter(actor, base)) {
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
            logger::trace("Releveling reference [{:X}]({}).", actor->GetFormID(), actor->GetName());

            std::string ezMessagePrefix;

            // 0x1E is a special encounter zone object that is used to indicate no EZ in some cases, treat same as no EZ
            // at all

            // priority:
            // 1. regular GetEncounterZone function
            // 2. read encounter zone from extra list
            // 3. read encounter zone from cell

            auto EZ = GetEncounterZone(actor);
            if (EZ && EZ->GetFormID() != 0x1E) {
                ezMessagePrefix = "Encounter zone found with function";
            } else {
                EZ = actor->extraList.GetEncounterZone();
                if (EZ && EZ->GetFormID() != 0x1E) {
                    ezMessagePrefix = "Encounter zone found in extra data";
                } else {
                    EZ = loadedData->encounterZone;
                    if (EZ && EZ->GetFormID() != 0x1E) {
                        ezMessagePrefix = "Encounter zone found in cell data";
                    } else {
                        EZ = NULL;
                    }
                }
            }

            if (!EZ) {
                if (settings->noZoneSkip) {
                    ResetActorbase(base);
                    logger::trace("    No encounter zone found, skipping NPC.");
                    return;
                } else {
                    ezMessagePrefix = "No encounter zone found, using iNoZoneMin and iNoZoneMax instead";
                }
            }

            // start with default min/max
            uint16_t minEZ = settings->noZoneMin;
            uint16_t maxEZ = settings->noZoneMax;

            // use encounter zone min/max, if valid
            if (EZ) {
                minEZ = EZ->data.minLevel;
                maxEZ = EZ->data.maxLevel;
            }
            if (minEZ < 1) {
                minEZ = 1;
            }
            if (maxEZ < 1) {
                maxEZ = 0;
            }
            std::string levelRange;
            if (maxEZ == 0) {
                levelRange = std::to_string(minEZ) + "+";
            } else {
                levelRange = std::to_string(minEZ) + "-" + std::to_string(maxEZ);
            }

            if (EZ) {
                logger::trace("    {}: [{:X}] ({})", ezMessagePrefix, EZ->GetFormID(), levelRange);
            } else {
                logger::trace("    {}: ({})", ezMessagePrefix, levelRange);
            }

            std::lock_guard<std::mutex> guard(_lock);
            RelevelActorbase(base, minEZ, maxEZ);

            if (settings->forceAutoCalcAttributes) {
                auto factory = IFormFactory::GetConcreteFormFactoryByType<Script>();
                if (factory) {
                    auto consoleScript = factory->Create();
                    if (consoleScript) {
                        // the setlevel command forces recalculation of attributes (health, magicka, stamina)
                        auto commandStr = "setlevel " + std::to_string(base->actorData.level) + " 0 " +
                                          std::to_string(base->actorData.calcLevelMin) + " " +
                                          std::to_string(base->actorData.calcLevelMax) + "";
                        consoleScript->SetCommand(commandStr);
                        consoleScript->CompileAndRun(actor);
                        delete consoleScript;
                    }
                }
            }
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
