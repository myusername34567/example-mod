#include <Geode/Geode.hpp>

// Geode Hooks & Bindings
#include <Geode/modify/InfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>

#include <Geode/binding/InfoLayer.hpp>
#include <Geode/binding/CommentCell.hpp>
#include <Geode/binding/GJComment.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelPage.hpp>
#include <Geode/binding/TableView.hpp>
#include <Geode/binding/CCContentLayer.hpp>

#include <matjson.hpp>
#include <algorithm>
#include <map>
#include <string>

using namespace geode::prelude;

// ============================================================================
// DATA STRUCTURES & MANAGER CLASS (CustomMLManager)
// ============================================================================

struct ReplacedLevelData {
    int mainLevelID = 0;             // e.g., 1 for Stereo Madness
    std::string originalName;
    std::string customLevelString;   // Encoded level string
    std::string customLevelName;
};

class CustomMLManager {
public:
    static CustomMLManager* get() {
        static CustomMLManager instance;
        return &instance;
    }

    std::map<int, ReplacedLevelData> m_replacedLevels;

    // Returns path to: <GeodeSaveDir>/CustomML/
    std::filesystem::path getCustomMLDir() {
        auto dir = Mod::get()->getSaveDir() / "CustomML";
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        return dir;
    }

    void setReplacement(int mainLevelID, GJGameLevel* customLevel) {
        if (!customLevel) return;

        ReplacedLevelData data;
        data.mainLevelID = mainLevelID;
        data.customLevelName = customLevel->m_levelName;
        data.customLevelString = customLevel->m_levelString;

        m_replacedLevels[mainLevelID] = data;
    }

    void revertMainLevel(int mainLevelID) {
        m_replacedLevels.erase(mainLevelID);
    }

    void revertAll() {
        m_replacedLevels.clear();
    }

    bool isReplaced(int mainLevelID) const {
        return m_replacedLevels.find(mainLevelID) != m_replacedLevels.end();
    }

    // Export ML Preset to CustomML folder
    bool exportPreset(const std::string& packName) {
        matjson::Value root = matjson::Object();
        matjson::Value levelsArray = matjson::Array();

        for (auto const& [id, data] : m_replacedLevels) {
            matjson::Value item = matjson::Object();
            item["mainLevelID"] = id;
            item["customName"] = data.customLevelName;
            item["levelData"] = data.customLevelString;
            levelsArray.push_back(item);
        }

        root["packName"] = packName;
        root["replacements"] = levelsArray;

        auto filePath = getCustomMLDir() / (packName + ".json");
        return utils::file::writeString(filePath, root.dump(2)).isOk();
    }

    // Import ML Preset from CustomML folder
    bool importPreset(const std::string& packName) {
        auto filePath = getCustomMLDir() / (packName + ".json");
        auto readResult = utils::file::readString(filePath);
        if (!readResult) return false;

        auto res = matjson::parse(readResult.value());
        if (!res) return false;

        m_replacedLevels.clear();
        auto root = res.value();

        if (root.contains("replacements") && root["replacements"].is_array()) {
            for (auto const& item : root["replacements"].asArray().value()) {
                ReplacedLevelData data;
                data.mainLevelID = item["mainLevelID"].asInt().value();
                data.customLevelName = item["customName"].asString().value();
                data.customLevelString = item["levelData"].asString().value();

                m_replacedLevels[data.mainLevelID] = data;
            }
        }
        return true;
    }
};

// ============================================================================
// FEATURE 1: COMMENT SEARCH ENGINE HOOK
// ============================================================================

class $modify(SearchableInfoLayer, InfoLayer) {
    struct Fields {
        TextInput* m_searchInput = nullptr;
        std::string m_currentFilter = "";
    };

    bool init(GJGameLevel* level, GJUserScore* score, bool p2) {
        if (!InfoLayer::init(level, score, p2)) return false;

        // Container menu registered with NodeIDs tag
        auto menu = CCMenu::create();
        menu->setID("comment-search-menu"_spr);

        // Geode TextInput for real-time comment filtering
        auto input = TextInput::create(140.0f, "Search comments...", "chatFont.fnt");
        input->setID("comment-search-input"_spr);
        input->setCallback([this](const std::string& text) {
            m_fields->m_currentFilter = text;
            this->filterComments();
        });

        menu->addChild(input);

        // Attach near the top right of the list container safely
        if (auto listLayer = this->getChildByID("comments-list")) {
            menu->setPosition({ listLayer->getPositionX() + 100.0f, listLayer->getPositionY() + 140.0f });
        } else {
            menu->setPosition({ 280.0f, 280.0f });
        }

        if (this->m_mainLayer) {
            this->m_mainLayer->addChild(menu);
        }
        m_fields->m_searchInput = input;

        return true;
    }

    void filterComments() {
        if (!m_listLayer || !m_listLayer->m_listView) return;

        auto contentLayer = m_listLayer->m_listView->m_contentLayer;
        if (!contentLayer) return;

        std::string filter = m_fields->m_currentFilter;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        for (auto child : CCArrayExt<CCNode*>(contentLayer->getChildren())) {
            if (auto cell = typeinfo_cast<CommentCell*>(child)) {
                if (!cell->m_comment) continue;

                std::string commentText = cell->m_comment->m_commentString;
                std::transform(commentText.begin(), commentText.end(), commentText.begin(), ::tolower);

                // Hide cell if it doesn't match current keyword
                bool matches = filter.empty() || (commentText.find(filter) != std::string::npos);
                cell->setVisible(matches);
            }
        }

        contentLayer->updateLayout();
    }
};

// ============================================================================
// FEATURE 2: MAIN LEVEL SWAPPING HOOKS & UI
// ============================================================================

// PlayLayer hook: Swaps level strings dynamically upon starting a main level
class $modify(MLPlayLayer, PlayLayer) {
    static PlayLayer* create(GJGameLevel* level, bool useReplay, bool dontPost) {
        if (level && level->m_levelType == GJLevelType::Local) {
            int levelID = level->m_levelID.value();

            if (CustomMLManager::get()->isReplaced(levelID)) {
                auto const& overrideData = CustomMLManager::get()->m_replacedLevels[levelID];
                
                // Override raw level string and name on loading
                level->m_levelString = overrideData.customLevelString;
                level->m_levelName = overrideData.customLevelName;
            }
        }
        return PlayLayer::create(level, useReplay, dontPost);
    }
};

// LevelSelectLayer hook: Injects CustomML management button onto the screen
class $modify(MLLevelSelectLayer, LevelSelectLayer) {
    bool init(int page) {
        if (!LevelSelectLayer::init(page)) return false;

        auto menu = CCMenu::create();
        menu->setID("custom-ml-menu"_spr);

        auto btnSprite = ButtonSprite::create("CustomML", "goldFont.fnt", "GJ_button_01.png", 0.8f);
        auto btn = CCMenuItemSpriteExtra::create(
            btnSprite,
            this,
            menu_selector(MLLevelSelectLayer::onOpenCustomML)
        );
        btn->setID("custom-ml-button"_spr);

        menu->addChild(btn);
        menu->setPosition({ 50.0f, 50.0f });

        this->addChild(menu);
        return true;
    }

    void onOpenCustomML(CCObject* sender) {
        std::string statusMessage = "CustomML Directory Active.\nSave directory:\n" + 
                                    CustomMLManager::get()->getCustomMLDir().string();

        FLAlertLayer::create(
            "CustomML Manager",
            statusMessage.c_str(),
            "OK"
        )->show();
    }
};
