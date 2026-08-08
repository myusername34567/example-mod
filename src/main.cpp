#include <Geode/Geode.hpp>
#include <Geode/modify/InfoLayer.hpp>
#include <Geode/modify/GameLevelManager.hpp>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <string>

using namespace geode::prelude;

// ============================================================================
// 1. CUSTOM MAIN LEVEL MANAGER (CustomML)
// ============================================================================

namespace CustomML {
    // Structure to hold custom level override info
    struct LevelOverride {
        int targetMainLevelID; // e.g. 1 for Stereo Madness
        int customLevelID;     // The custom created/online level ID
        bool isOnlineLevel;    // true = downloaded level, false = created level
    };

    // Active replacements map: <MainLevelID, OverrideData>
    inline std::unordered_map<int, LevelOverride> activeOverrides;

    // Helper to get or create <geode_save_dir>/CustomML/ folder
    inline std::filesystem::path getFolder() {
        auto path = Mod::get()->getSaveDir() / "CustomML";
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
        }
        return path;
    }

    // Set a level replacement
    inline void setOverride(int mainLevelID, int customLevelID, bool isOnline) {
        activeOverrides[mainLevelID] = { mainLevelID, customLevelID, isOnline };
    }

    // Clear all overrides (Revert to original main levels)
    inline void revertAll() {
        activeOverrides.clear();
        log::info("All main levels reverted to default.");
    }

    // Save custom ML pack to JSON file for sharing
    inline bool savePack(std::string const& packName) {
        matjson::Value root = matjson::Value::object();
        matjson::Value levels = matjson::Value::array();

        for (auto const& [mainID, overrideData] : activeOverrides) {
            matjson::Value item = matjson::Value::object();
            item["main_id"] = overrideData.targetMainLevelID;
            item["custom_id"] = overrideData.customLevelID;
            item["is_online"] = overrideData.isOnlineLevel;
            levels.push(item);
        }

        root["pack_name"] = packName;
        root["levels"] = levels;

        auto filePath = getFolder() / (packName + ".json");
        std::ofstream file(filePath);
        if (!file.is_open()) return false;

        file << root.dump(matjson::NO_INDENTATION);
        log::info("Saved CustomML pack to {}", filePath.string());
        return true;
    }

    // Load custom ML pack from a JSON file in CustomML folder
    inline bool loadPack(std::string const& packName) {
        auto filePath = getFolder() / (packName + ".json");
        if (!std::filesystem::exists(filePath)) return false;

        std::ifstream file(filePath);
        if (!file.is_open()) return false;

        // FIX 1: matjson::parse returns a geode::Result, not std::optional.
        // Use isOk()/unwrap() instead of has_value()/value().
        auto parseResult = matjson::parse(file);
        if (!parseResult.isOk()) return false;

        auto root = parseResult.unwrap();
        if (!root.contains("levels") || !root["levels"].is_array()) return false;

        activeOverrides.clear();

        for (auto const& item : root["levels"].as_array().value()) {
            if (item.contains("main_id") && item.contains("custom_id")) {
                int mainID = item["main_id"].as_int().value();
                int customID = item["custom_id"].as_int().value();
                bool isOnline = item.contains("is_online") ? item["is_online"].as_bool().value() : true;

                setOverride(mainID, customID, isOnline);
            }
        }

        log::info("Loaded CustomML pack from {}", filePath.string());
        return true;
    }
}

// FIX 4: There is no GameManager::getGJMGL in the current bindings.
// Main levels are actually retrieved through GameLevelManager::getMainLevel,
// so we hook that instead of a nonexistent GameManager member.
class $modify(CustomMLGameLevelManager, GameLevelManager) {
    GJGameLevel* getMainLevel(int levelID, bool dontGetLevelString) {
        // Check if there's an active override for this main level ID
        if (CustomML::activeOverrides.contains(levelID)) {
            auto const& overrideData = CustomML::activeOverrides[levelID];

            GJGameLevel* customLevel = nullptr;

            if (overrideData.isOnlineLevel) {
                customLevel = this->getSavedLevel(overrideData.customLevelID);
            } else {
                // FIX 2/3: There is no m_localLevels member on GameLevelManager.
                // Player-created levels are fetched via getSavedLevels(), which
                // returns a CCArray*, so the CCArrayExt wrap below works directly.
                auto localLevels = this->getSavedLevels(false, 0);
                if (localLevels) {
                    for (auto item : CCArrayExt<GJGameLevel*>(localLevels)) {
                        if (item && item->m_levelID == overrideData.customLevelID) {
                            customLevel = item;
                            break;
                        }
                    }
                }
            }

            if (customLevel) {
                log::info("Replaced Main Level {} with Custom Level {}", levelID, overrideData.customLevelID);
                return customLevel;
            }
        }

        // Fallback to original GD 2.2081 main level logic
        return GameLevelManager::getMainLevel(levelID, dontGetLevelString);
    }
};

// ============================================================================
// 2. COMMENT SEARCH IN INFOLAYER (NodeIDs Safe)
// ============================================================================

class $modify(SearchInfoLayer, InfoLayer) {
    struct Fields {
        TextInput* m_searchInput = nullptr;
    };

    // FIX 5: InfoLayer::init's third parameter is a GJLevelList*, not a bool.
    bool init(GJGameLevel* level, GJUserScore* score, GJLevelList* list) {
        if (!InfoLayer::init(level, score, list)) return false;

        // Obtain or safely construct a node-ids compliant container menu
        auto menu = this->getChildByID("main-menu");
        if (!menu) {
            menu = CCMenu::create();
            menu->setID("comment-search-menu");
            this->addChild(menu);
        }

        // Create Search Box
        auto searchInput = TextInput::create(140.f, "Search...", "chatFont.fnt");
        searchInput->setID("comment-search-input");
        searchInput->setScale(0.7f);

        // Position on top-right of comment panel safely
        searchInput->setPosition(ccp(120.f, 135.f));

        // Text input callback filter
        searchInput->setCallback([this](std::string const& text) {
            this->filterComments(text);
        });

        m_fields->m_searchInput = searchInput;
        menu->addChild(searchInput);

        return true;
    }

    void filterComments(std::string const& query) {
        // FIX 6: InfoLayer has no m_listLayer member. The comment list is
        // InfoLayer::m_list (a GJCommentListLayer*), which itself wraps a
        // BoomListView* in its own m_list field. The BoomListView's
        // underlying TableView (m_tableView) holds the actual cell nodes
        // in m_contentLayer.
        if (!m_list || !m_list->m_list) return;

        auto content = m_list->m_list->m_tableView ? m_list->m_list->m_tableView->m_contentLayer : nullptr;
        if (!content) return;

        std::string lowerQuery = query;
        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

        for (auto child : CCArrayExt<CCNode*>(content->getChildren())) {
            auto cell = typeinfo_cast<CommentCell*>(child);
            if (!cell || !cell->m_comment) continue;

            std::string commentText = cell->m_comment->m_commentString;
            std::transform(commentText.begin(), commentText.end(), commentText.begin(), ::tolower);

            // Toggle cell visibility depending on search match
            if (lowerQuery.empty() || commentText.find(lowerQuery) != std::string::npos) {
                cell->setVisible(true);
            } else {
                cell->setVisible(false);
            }
        }

        // Re-arrange layout dynamically
        content->updateLayout();
    }
};
