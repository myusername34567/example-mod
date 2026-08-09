#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <unordered_set>
#include <cmath>
#include <algorithm>

using namespace geode::prelude;

// Better AutoBuild
// A recreation of Antiprime's AutoBuild mod: toggle a mode in the editor
// that lets you click-and-drag to stamp the currently selected build
// object along the grid, instead of placing one object per tap.

class $modify(BABEditorUI, EditorUI) {
    struct Fields {
        bool autoBuildActive = false;
        int currentObjectID = 1;
        CCMenuItemSpriteExtra* autoBuildBtn = nullptr;
        std::unordered_set<int64_t> visitedCells;
        CCPoint lastRawTouchPos = { 0.f, 0.f };
        bool hasLastTouch = false;
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto offSprite = ButtonSprite::create("AB", "bigFont.fnt", "GJ_button_04.png", 0.8f);
        offSprite->setScale(0.65f);

        m_fields->autoBuildBtn = CCMenuItemSpriteExtra::create(
            offSprite, this, menu_selector(BABEditorUI::onToggleAutoBuild)
        );
        m_fields->autoBuildBtn->setID("better-autobuild-toggle-btn"_spr);

        auto menu = CCMenu::create();
        menu->addChild(m_fields->autoBuildBtn);
        menu->setPosition({ winSize.width - 30.f, winSize.height - 120.f });
        menu->setZOrder(200);
        menu->setID("better-autobuild-menu"_spr);
        this->addChild(menu, 200);

        return true;
    }

    // Track whatever build item the player last picked from the create menu,
    // so AutoBuild knows what to stamp along the drag.
    void onCreateObject(int id) {
        EditorUI::onCreateObject(id);
        m_fields->currentObjectID = id;
    }

    void onToggleAutoBuild(CCObject*) {
        m_fields->autoBuildActive = !m_fields->autoBuildActive;
        m_fields->visitedCells.clear();
        m_fields->hasLastTouch = false;

        auto sprite = static_cast<ButtonSprite*>(m_fields->autoBuildBtn->getNormalImage());
        if (m_fields->autoBuildActive) {
            auto color = Mod::get()->getSettingValue<ccColor3B>("auto-select-color");
            sprite->setColor(color);
        } else {
            sprite->setColor({ 255, 255, 255 });
        }
    }

    float babGridSize() {
        return m_gridSize > 0.f ? m_gridSize : 30.f;
    }

    int64_t babCellKey(CCPoint snapped) {
        float g = babGridSize();
        int64_t gx = static_cast<int64_t>(std::lround(snapped.x / g));
        int64_t gy = static_cast<int64_t>(std::lround(snapped.y / g));
        return (gx << 32) ^ (gy & 0xffffffffLL);
    }

    void babPlaceIfNew(CCPoint rawPos) {
        if (m_fields->currentObjectID <= 0) return;

        auto snapped = this->getGridSnappedPos(rawPos);
        auto key = babCellKey(snapped);
        if (m_fields->visitedCells.count(key)) return;
        m_fields->visitedCells.insert(key);

        this->createObject(m_fields->currentObjectID, snapped);
    }

    // Samples points between two raw touch positions so a fast drag doesn't
    // leave gaps between placed objects, then snaps + dedupes each sample.
    void babPlaceAlongLine(CCPoint from, CCPoint to) {
        float g = babGridSize();
        float fraction = static_cast<float>(Mod::get()->getSettingValue<double>("step-fraction"));
        float step = std::max(g * fraction, 2.f);

        float dist = ccpDistance(from, to);
        int steps = static_cast<int>(dist / step) + 1;

        for (int i = 0; i <= steps; i++) {
            float t = steps == 0 ? 0.f : static_cast<float>(i) / static_cast<float>(steps);
            babPlaceIfNew(ccpLerp(from, to, t));
        }
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        if (m_fields->autoBuildActive) {
            auto pos = this->getTouchPoint(touch, event);
            m_fields->visitedCells.clear();
            babPlaceIfNew(pos);
            m_fields->lastRawTouchPos = pos;
            m_fields->hasLastTouch = true;
            return true;
        }
        return EditorUI::ccTouchBegan(touch, event);
    }

    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (m_fields->autoBuildActive) {
            auto pos = this->getTouchPoint(touch, event);
            if (m_fields->hasLastTouch) {
                babPlaceAlongLine(m_fields->lastRawTouchPos, pos);
            } else {
                babPlaceIfNew(pos);
            }
            m_fields->lastRawTouchPos = pos;
            m_fields->hasLastTouch = true;
            return;
        }
        EditorUI::ccTouchMoved(touch, event);
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        if (m_fields->autoBuildActive) {
            m_fields->visitedCells.clear();
            m_fields->hasLastTouch = false;
            return;
        }
        EditorUI::ccTouchEnded(touch, event);
    }

    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        if (m_fields->autoBuildActive) {
            m_fields->visitedCells.clear();
            m_fields->hasLastTouch = false;
            return;
        }
        EditorUI::ccTouchCancelled(touch, event);
    }
};
