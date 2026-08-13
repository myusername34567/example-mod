#include <Geode/Geode.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/cocos/menu_nodes/CCMenu.h>

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

struct TBLIBPiece {
    std::string name;
    std::string objects;
    size_t objectCount = 0;
};

struct TBLIBLibrary {
    std::string name;
    std::string algorithm;
    std::vector<TBLIBPiece> pieces;
};

static std::vector<TBLIBLibrary> g_libraries;
static bool g_loaded = false;

static std::vector<std::string> splitLines(std::string const& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
    }
    return out;
}

static std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

static bool isSerializedObject(std::string const& line) {
    // TBLIB object records are comma-separated key/value pairs and start with key 1.
    return line.rfind("1,", 0) == 0;
}

static void parsePieceBlock(
    TBLIBLibrary& lib,
    std::vector<std::string> const& block,
    size_t index
) {
    if (block.empty())
        return;

    std::string objectData;
    size_t objectCount = 0;

    for (auto const& raw : block) {
        auto line = trim(raw);
        if (!isSerializedObject(line))
            continue;

        objectData += line;
        if (!objectData.empty() && objectData.back() != ';')
            objectData.push_back(';');

        // A single TBLIB line can contain multiple object records separated by ';'.
        for (char c : line)
            if (c == ';')
                ++objectCount;
    }

    if (objectData.empty())
        return;

    TBLIBPiece piece;
    piece.name = fmt::format("{} #{}", lib.name, index);
    piece.objects = std::move(objectData);
    piece.objectCount = objectCount;
    lib.pieces.push_back(std::move(piece));
}

static void loadLibraries() {
    if (g_loaded)
        return;
    g_loaded = true;

    auto dir = Mod::get()->getResourcesDir() / "templates";
    std::vector<std::string> files = {
        "hell_temp.tblib",
        "hell_base.tblib",
        "modern_base.tblib",
        "modern_temp.tblib",
        "null_base.tblib",
        "null_temp.tblib",
        "pt_base.tblib",
        "tech_temp.tblib",
        "wfc_base.tblib",
        "tech_base.tblib"
    };

    for (auto const& file : files) {
        auto path = dir / file;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            continue;

        std::stringstream buffer;
        buffer << in.rdbuf();
        auto lines = splitLines(buffer.str());

        TBLIBLibrary lib;
        lib.name = file.substr(0, file.find(".tblib"));

        if (lines.size() > 1 && lines[1].rfind("ALG ", 0) == 0)
            lib.algorithm = lines[1].substr(4);

        auto piecesIt = std::find_if(lines.begin(), lines.end(), [](std::string const& l) {
            return l.rfind("PIECES ", 0) == 0;
        });
        if (piecesIt == lines.end())
            continue;

        std::vector<std::string> block;
        size_t pieceIndex = 0;
        bool first = true;

        // PIECES blocks are separated by --- and continue until the next
        // top-level section such as PM/FILLERS/Rxxx.
        for (size_t i = static_cast<size_t>(piecesIt - lines.begin()) + 1; i < lines.size(); ++i) {
            auto const& line = lines[i];

            if (line == "---") {
                if (!block.empty()) {
                    parsePieceBlock(lib, block, pieceIndex++);
                    block.clear();
                }
                continue;
            }

            // A top-level non-numeric section marks the end of PIECES.
            bool startsNumeric = !line.empty() &&
                (std::isdigit(static_cast<unsigned char>(line[0])) || line[0] == '-');

            if (!startsNumeric && line.rfind("PIECES ", 0) != 0) {
                if (!block.empty())
                    parsePieceBlock(lib, block, pieceIndex++);
                break;
            }

            if (!first || line.rfind("PIECES ", 0) != 0)
                block.push_back(line);
            first = false;
        }

        if (!block.empty())
            parsePieceBlock(lib, block, pieceIndex++);

        if (!lib.pieces.empty())
            g_libraries.push_back(std::move(lib));
    }

    log::info("Loaded {} TBLIB libraries / {} pieces",
        g_libraries.size(),
        std::accumulate(g_libraries.begin(), g_libraries.end(), size_t{0},
            [](size_t n, TBLIBLibrary const& l) { return n + l.pieces.size(); }));
}

class TemplatePopup : public geode::Popup {
protected:
    LevelEditorLayer* m_editor = nullptr;
    geode::TextInput* m_query = nullptr;
    geode::TextInput* m_x = nullptr;
    geode::TextInput* m_y = nullptr;

    bool init(LevelEditorLayer* editor) {
        if (!Popup::init(360.f, 230.f))
            return false;

        m_editor = editor;
        this->setTitle("TBLIB AutoBuild");

        auto info = CCLabelBMFont::create(
            "Enter library:piece  (example: hell_temp:0)",
            "goldFont.fnt"
        );
        info->setScale(.42f);
        info->setPosition({180.f, 180.f});
        m_mainLayer->addChild(info);

        m_query = geode::TextInput::create(300.f, "library:piece", "bigFont.fnt");
        m_query->setPosition({180.f, 140.f});
        m_query->setMaxCharCount(48);
        m_mainLayer->addChild(m_query);

        m_x = geode::TextInput::create(135.f, "X (blank = click)", "bigFont.fnt");
        m_x->setPosition({105.f, 95.f});
        m_x->setMaxCharCount(16);
        m_mainLayer->addChild(m_x);

        m_y = geode::TextInput::create(135.f, "Y (blank = click)", "bigFont.fnt");
        m_y->setPosition({255.f, 95.f});
        m_y->setMaxCharCount(16);
        m_mainLayer->addChild(m_y);

        auto menu = CCMenu::create();
        menu->setPosition({180.f, 45.f});
        m_mainLayer->addChild(menu);

        auto place = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Place"),
            this,
            menu_selector(TemplatePopup::onPlace)
        );
        menu->addChild(place);

        auto list = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("List"),
            this,
            menu_selector(TemplatePopup::onList)
        );
        list->setPosition({-105.f, 0.f});
        menu->addChild(list);

        return true;
    }

    static bool parseFloat(std::string const& s, float& out) {
        if (s.empty())
            return false;
        try {
            size_t used = 0;
            out = std::stof(s, &used);
            return used == s.size();
        } catch (...) {
            return false;
        }
    }

    bool parseSelection(std::string const& text, size_t& libIndex, size_t& pieceIndex) {
        auto colon = text.find(':');
        if (colon == std::string::npos)
            return false;

        auto libName = text.substr(0, colon);
        auto pieceText = text.substr(colon + 1);

        try {
            pieceIndex = std::stoul(pieceText);
        } catch (...) {
            return false;
        }

        for (size_t i = 0; i < g_libraries.size(); ++i) {
            if (g_libraries[i].name == libName) {
                libIndex = i;
                return pieceIndex < g_libraries[i].pieces.size();
            }
        }
        return false;
    }

    void onPlace(CCObject*) {
        auto selection = m_query->getString();
        size_t li = 0, pi = 0;

        if (!parseSelection(selection, li, pi)) {
            FLAlertLayer::create(
                "TBLIB AutoBuild",
                "Invalid template. Use a name such as <cy>hell_temp:0</c>.",
                "OK"
            )->show();
            return;
        }

        auto const& piece = g_libraries[li].pieces[pi];

        // EditorUI::pasteObjects already understands GD's normal object
        // serialization, which is the format stored inside TBLIB 2 PIECES.
        auto objects = m_editor->m_editorUI->pasteObjects(piece.objects, true, false);
        if (!objects || objects->count() == 0) {
            FLAlertLayer::create(
                "TBLIB AutoBuild",
                "The template contained no pasteable Geometry Dash objects.",
                "OK"
            )->show();
            return;
        }

        float x = 0.f, y = 0.f;
        auto xs = m_x->getString();
        auto ys = m_y->getString();

        bool haveX = parseFloat(xs, x);
        bool haveY = parseFloat(ys, y);

        if (haveX && haveY) {
            m_editor->m_editorUI->repositionObjectsToCenter(objects, {x, y}, true);
        } else {
            // m_clickAtPosition is updated by the normal editor interaction.
            auto target = m_editor->m_editorUI->m_clickAtPosition;
            m_editor->m_editorUI->repositionObjectsToCenter(objects, target, true);
        }

        m_editor->m_editorUI->selectObjects(objects, false);

        FLAlertLayer::create(
            "TBLIB AutoBuild",
            fmt::format("Placed <cy>{}</c> with {} objects.", piece.name, objects->count()).c_str(),
            "OK"
        )->show();

        this->onClose(nullptr);
    }

    void onList(CCObject*) {
        std::string text;
        for (auto const& lib : g_libraries) {
            text += fmt::format("<cy>{}</c>  [{} pieces]\\n", lib.name, lib.pieces.size());
        }
        text += "\\nUse <cy>library:piece</c>, for example <cy>tech_temp:3</c>.";
        FLAlertLayer::create("TBLIB Libraries", text.c_str(), "OK")->show();
    }

public:
    static TemplatePopup* create(LevelEditorLayer* editor) {
        auto ret = new TemplatePopup();
        if (ret->init(editor)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class $modify(TBLIBLevelEditor, LevelEditorLayer) {
    struct Fields {
        CCMenu* menu = nullptr;
    };

    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1))
            return false;

        loadLibraries();

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 9999);
        m_fields->menu = menu;

        auto button = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("TB"),
            this,
            menu_selector(TBLIBLevelEditor::onTBLIB)
        );
        button->setScale(.7f);

        auto size = CCDirector::sharedDirector()->getWinSize();
        button->setPosition({size.width - 55.f, size.height - 55.f});
        menu->addChild(button);

        return true;
    }

    void onTBLIB(CCObject*) {
        auto popup = TemplatePopup::create(this);
        if (popup)
            popup->show();
    }
};
