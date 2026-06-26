// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "data_navigator_window.h"

#include <imgui.h>
#include <cctype>
#include <algorithm>
#include <unordered_map>

#include "base/macros.h"
#include "editor_host.h"
#include "icon_font.h"
#include "editor_fonts.h"

// Include all editor window headers for type information
#include "spell_editor_window.h"
#include "quest_editor_window.h"
#include "item_editor_window.h"
#include "item_subclass_editor_window.h"
#include "item_class_editor_window.h"
#include "range_type_editor_window.h"
#include "aura_stacking_category_editor_window.h"
#include "creature_editor_window.h"
#include "class_editor_window.h"
#include "unit_class_editor_window.h"
#include "race_editor_window.h"
#include "proficiency_editor_window.h"
#include "chat_channel_editor_window.h"
#include "faction_editor_window.h"
#include "faction_template_editor_window.h"
#include "gossip_editor_window.h"
#include "model_editor_window.h"
#include "item_display_editor_window.h"
#include "object_display_editor_window.h"
#include "map_editor_window.h"
#include "object_editor_window.h"
#include "zone_editor_window.h"
#include "lock_type_editor_window.h"
#include "unit_loot_editor_window.h"
#include "trainer_editor_window.h"
#include "vendor_editor_window.h"
#include "combat_settings_editor_window.h"
#include "animation_editor_window.h"
#include "condition_editor_window.h"
#include "variable_editor_window.h"
#include "spell_visualization_editor_window.h"
#include "trigger_editor_window.h"
#include "talent_editor_window.h"

namespace mmo
{
    namespace
    {
        /// @brief Maps a category name to a representative icon glyph.
        const char* IconForCategory(const String& name)
        {
            if (name == "Gameplay")      return ICON_FA_BOLT;
            if (name == "Characters")    return ICON_FA_USERS;
            if (name == "Visuals")       return ICON_FA_PICTURE_O;
            if (name == "World")         return ICON_FA_GLOBE;
            if (name == "Miscellaneous") return ICON_FA_LIST;
            return ICON_FA_FOLDER;
        }

        /// @brief Maps an editor entry's display name to an icon glyph.
        const char* IconForEditor(const String& name)
        {
            static const std::unordered_map<String, const char*> icons = {
                { "Spells", ICON_FA_MAGIC }, { "Quests", ICON_FA_TROPHY }, { "Items", ICON_FA_SHIELD },
                { "Item Classes", ICON_FA_TAGS }, { "Item Subclasses", ICON_FA_TAG },
                { "Spell Range Types", ICON_FA_CROSSHAIRS }, { "Aura Stacking Categories", ICON_FA_LIST_UL },
                { "Lock Types", ICON_FA_LOCK }, { "Triggers", ICON_FA_BOLT }, { "Conditions", ICON_FA_CHECK_SQUARE_O },
                { "Creatures", ICON_FA_PAW }, { "Classes", ICON_FA_SHIELD }, { "Unit Classes", ICON_FA_USER },
                { "Races", ICON_FA_VENUS_MARS }, { "Proficiencies", ICON_FA_BRIEFCASE }, { "Talents", ICON_FA_STAR },
                { "Factions", ICON_FA_USERS }, { "Faction Templates", ICON_FA_USERS },
                { "Animations", ICON_FA_FILM }, { "Spell Visualizations", ICON_FA_MAGIC }, { "Models", ICON_FA_CUBE },
                { "Item Displays", ICON_FA_CUBE }, { "Object Displays", ICON_FA_CUBES },
                { "Maps", ICON_FA_MAP }, { "Objects", ICON_FA_CUBES }, { "Zones", ICON_FA_MAP_SIGNS },
                { "Unit Loot", ICON_FA_GIFT }, { "Trainers", ICON_FA_GRADUATION_CAP }, { "Vendors", ICON_FA_SHOPPING_CART },
                { "Gossip", ICON_FA_COMMENT }, { "Combat Settings", ICON_FA_COGS }, { "Variables", ICON_FA_CODE },
                { "Chat Channels", ICON_FA_COMMENTS },
            };

            const auto it = icons.find(name);
            return it != icons.end() ? it->second : ICON_FA_FILE_O;
        }
    }

    DataNavigatorWindow::DataNavigatorWindow(const String& name, proto::Project& project, EditorHost& host)
        : EditorWindowBase(name)
        , m_project(project)
        , m_host(host)
    {
        m_hasToolbarButton = true;
        m_toolbarButtonText = "Data Navigator";
        m_toolbarButtonIcon = ICON_FA_DATABASE;
        
        // Initialize the categories and editors list
        InitializeCategories();
    }

    bool DataNavigatorWindow::Draw()
    {
        if (ImGui::Begin(m_name.c_str(), &m_visible))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // Draw a search filter at the top, padded a touch for breathing room.
            ImGui::Spacing();
            static char searchBuffer[256] = "";
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
            ImGui::InputTextWithHint("##search", ICON_FA_SEARCH "  Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));
            ImGui::PopStyleVar();
            ImGui::Spacing();
            ImGui::Spacing();

            const String searchText = searchBuffer;
            const bool hasSearchFilter = !searchText.empty();

            // Calculate if we need to show the category name based on filtering
            bool showCategoryHeader = !hasSearchFilter;

            const ImU32 countColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 iconColor = ImGui::GetColorU32(ImVec4(0.337f, 0.624f, 0.824f, 1.0f));

            // Slightly roomier rows for a less cramped, more navigable list.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 3.0f));

            // Draw the categories and their editors
            for (auto& category : m_categories)
            {
                bool hasMatchingEditors = false;
                
                // First pass to see if we should show this category
                if (hasSearchFilter)
                {
                    for (const auto& editor : category.editors)
                    {
                        String lowerName = editor.displayName;
                        String lowerSearch = searchText;
                        
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                            [](unsigned char c){ return std::tolower(c); });
                        std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(),
                            [](unsigned char c){ return std::tolower(c); });
                        
                        if (lowerName.find(lowerSearch) != String::npos)
                        {
                            hasMatchingEditors = true;
                            break;
                        }
                    }
                    
                    if (!hasMatchingEditors)
                    {
                        continue; // Skip this category entirely
                    }
                }
                
                // Draw the category header if we're not searching
                if (showCategoryHeader)
                {
                    ImGuiTreeNodeFlags categoryFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                    const String headerLabel = String(IconForCategory(category.name)) + "   " + category.name;

                    if (ImFont* headerFont = GetEditorHeaderFont())
                    {
                        ImGui::PushFont(headerFont);
                    }
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));
                    category.isOpen = ImGui::CollapsingHeader(headerLabel.c_str(), categoryFlags);
                    ImGui::PopStyleVar();
                    if (GetEditorHeaderFont())
                    {
                        ImGui::PopFont();
                    }
                }
                
                // If the category is open or we're filtering, draw its editors
                if (category.isOpen || hasSearchFilter)
                {
                    for (const auto& editor : category.editors)
                    {
                        // Skip if we're filtering and this doesn't match
                        if (hasSearchFilter)
                        {
                            String lowerName = editor.displayName;
                            String lowerSearch = searchText;
                            
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                [](unsigned char c){ return std::tolower(c); });
                            std::transform(lowerSearch.begin(), lowerSearch.end(), lowerSearch.begin(),
                                [](unsigned char c){ return std::tolower(c); });
                            
                            if (lowerName.find(lowerSearch) == String::npos)
                            {
                                continue;
                            }
                        }
                        
                        // Add indentation when showing categories
                        if (showCategoryHeader)
                        {
                            ImGui::Indent();
                        }

                        String idStr = editor.displayName;
                        ImGui::PushID(idStr.c_str());

                        // Capture the row geometry before drawing so we can overlay the icon and a
                        // right-aligned, dimmed item count on top of the selectable.
                        const float rowWidth = ImGui::GetContentRegionAvail().x;
                        const ImVec2 rowMin = ImGui::GetCursorScreenPos();

                        // Leading spaces reserve room for the icon we paint manually.
                        const String label = "      " + editor.displayName;
                        if (ImGui::Selectable(label.c_str()))
                        {
                            // Call the callback to open this editor
                            if (editor.openCallback)
                            {
                                editor.openCallback();
                            }
                        }

                        // Paint the per-entry icon in the editor accent color.
                        drawList->AddText(ImVec2(rowMin.x + 2.0f, rowMin.y), iconColor, IconForEditor(editor.displayName));

                        // Right-aligned, dimmed item count.
                        if (editor.count > 0)
                        {
                            const String countText = std::to_string(editor.count);
                            const float countWidth = ImGui::CalcTextSize(countText.c_str()).x;
                            drawList->AddText(ImVec2(rowMin.x + rowWidth - countWidth - 2.0f, rowMin.y), countColor, countText.c_str());
                        }

                        ImGui::PopID();

                        // Remove indentation
                        if (showCategoryHeader)
                        {
                            ImGui::Unindent();
                        }
                    }

                    // A little breathing room between categories.
                    if (showCategoryHeader)
                    {
                        ImGui::Spacing();
                    }
                }
            }

            ImGui::PopStyleVar(); // ItemSpacing
        }
        ImGui::End();

        return false;
    }

    void DataNavigatorWindow::OpenEditorWindow(const std::type_index& typeIndex)
    {
        // Call the callback if assigned
        if (m_openEditorWindowSignal)
        {
            m_openEditorWindowSignal(typeIndex);
        }
    }

    void DataNavigatorWindow::InitializeCategories()
    {
        // Create categories to organize the editors
        DataCategory gameplayCategory;
        gameplayCategory.name = "Gameplay";
        
        DataCategory characterCategory;
        characterCategory.name = "Characters";
        
        DataCategory visualsCategory;
        visualsCategory.name = "Visuals";
        
        DataCategory worldCategory;
        worldCategory.name = "World";
        
        DataCategory miscCategory;
        miscCategory.name = "Miscellaneous";
        
        // Add editors to the gameplay category
        gameplayCategory.editors.push_back({
            std::type_index(typeid(SpellEditorWindow)),
            "Spells",
            [this]() { OpenEditorWindow(std::type_index(typeid(SpellEditorWindow))); },
            static_cast<int>(m_project.spells.count())
        });
        
        gameplayCategory.editors.push_back({
            std::type_index(typeid(QuestEditorWindow)),
            "Quests",
            [this]() { OpenEditorWindow(std::type_index(typeid(QuestEditorWindow))); },
            static_cast<int>(m_project.quests.count())
        });
        
        gameplayCategory.editors.push_back({
            std::type_index(typeid(ItemEditorWindow)),
            "Items",
            [this]() { OpenEditorWindow(std::type_index(typeid(ItemEditorWindow))); },
            static_cast<int>(m_project.items.count())
        });
        
        gameplayCategory.editors.push_back({
            std::type_index(typeid(ItemClassEditorWindow)),
            "Item Classes",
            [this]() { OpenEditorWindow(std::type_index(typeid(ItemClassEditorWindow))); },
            static_cast<int>(m_project.itemClasses.count())
        });
        
        gameplayCategory.editors.push_back({
            std::type_index(typeid(ItemSubclassEditorWindow)),
            "Item Subclasses",
            [this]() { OpenEditorWindow(std::type_index(typeid(ItemSubclassEditorWindow))); },
            static_cast<int>(m_project.itemSubclasses.count())
        });
        
        gameplayCategory.editors.push_back({
            std::type_index(typeid(RangeTypeEditorWindow)),
            "Spell Range Types",
            [this]() { OpenEditorWindow(std::type_index(typeid(RangeTypeEditorWindow))); },
            static_cast<int>(m_project.ranges.count())
        });

        gameplayCategory.editors.push_back({
            std::type_index(typeid(AuraStackingCategoryEditorWindow)),
            "Aura Stacking Categories",
            [this]() { OpenEditorWindow(std::type_index(typeid(AuraStackingCategoryEditorWindow))); },
            static_cast<int>(m_project.auraStackingCategories.count())
        });

        gameplayCategory.editors.push_back({
            std::type_index(typeid(LockTypeEditorWindow)),
            "Lock Types",
            [this]() { OpenEditorWindow(std::type_index(typeid(LockTypeEditorWindow))); },
            static_cast<int>(m_project.lockTypes.count())
        });

        gameplayCategory.editors.push_back({
            std::type_index(typeid(TriggerEditorWindow)),
            "Triggers",
            [this]() { OpenEditorWindow(std::type_index(typeid(TriggerEditorWindow))); },
            static_cast<int>(m_project.triggers.count())
        });

        gameplayCategory.editors.push_back({
            std::type_index(typeid(ConditionEditorWindow)),
            "Conditions",
            [this]() { OpenEditorWindow(std::type_index(typeid(ConditionEditorWindow))); },
            static_cast<int>(m_project.conditions.count())
        });
        
        // Add editors to the character category
        characterCategory.editors.push_back({
            std::type_index(typeid(CreatureEditorWindow)),
            "Creatures",
            [this]() { OpenEditorWindow(std::type_index(typeid(CreatureEditorWindow))); },
            static_cast<int>(m_project.units.count())
        });
          characterCategory.editors.push_back({
            std::type_index(typeid(ClassEditorWindow)),
            "Classes",
            [this]() { OpenEditorWindow(std::type_index(typeid(ClassEditorWindow))); },
            static_cast<int>(m_project.classes.count())
        });
        
        characterCategory.editors.push_back({
            std::type_index(typeid(UnitClassEditorWindow)),
            "Unit Classes",
            [this]() { OpenEditorWindow(std::type_index(typeid(UnitClassEditorWindow))); },
            static_cast<int>(m_project.unitClasses.count())
        });
        
        characterCategory.editors.push_back({
            std::type_index(typeid(RaceEditorWindow)),
            "Races",
            [this]() { OpenEditorWindow(std::type_index(typeid(RaceEditorWindow))); },
            static_cast<int>(m_project.races.count())
        });
        
        characterCategory.editors.push_back({
            std::type_index(typeid(ProficiencyEditorWindow)),
            "Proficiencies",
            [this]() { OpenEditorWindow(std::type_index(typeid(ProficiencyEditorWindow))); },
            static_cast<int>(m_project.proficiencies.count())
        });
        
        characterCategory.editors.push_back({
            std::type_index(typeid(TalentEditorWindow)),
            "Talents",
            [this]() { OpenEditorWindow(std::type_index(typeid(TalentEditorWindow))); },
            static_cast<int>(m_project.talentTabs.count())
        });

        characterCategory.editors.push_back({
            std::type_index(typeid(FactionEditorWindow)),
            "Factions",
            [this]() { OpenEditorWindow(std::type_index(typeid(FactionEditorWindow))); },
            static_cast<int>(m_project.factions.count())
        });
        
        characterCategory.editors.push_back({
            std::type_index(typeid(FactionTemplateEditorWindow)),
            "Faction Templates",
            [this]() { OpenEditorWindow(std::type_index(typeid(FactionTemplateEditorWindow))); },
            static_cast<int>(m_project.factionTemplates.count())
        });
        
        // Add editors to the visuals category
        visualsCategory.editors.push_back({
            std::type_index(typeid(AnimationEditorWindow)),
            "Animations",
            [this]() { OpenEditorWindow(std::type_index(typeid(AnimationEditorWindow))); },
            static_cast<int>(m_project.animations.count())
        });

        visualsCategory.editors.push_back({
            std::type_index(typeid(SpellVisualizationEditorWindow)),
            "Spell Visualizations",
            [this]() { OpenEditorWindow(std::type_index(typeid(SpellVisualizationEditorWindow))); },
            static_cast<int>(m_project.spellVisualizations.count())
        });

        visualsCategory.editors.push_back({
            std::type_index(typeid(ModelEditorWindow)),
            "Models",
            [this]() { OpenEditorWindow(std::type_index(typeid(ModelEditorWindow))); },
            static_cast<int>(m_project.models.count())
        });
        
        visualsCategory.editors.push_back({
            std::type_index(typeid(ItemDisplayEditorWindow)),
            "Item Displays",
            [this]() { OpenEditorWindow(std::type_index(typeid(ItemDisplayEditorWindow))); },
            static_cast<int>(m_project.itemDisplays.count())
        });
        
        visualsCategory.editors.push_back({
            std::type_index(typeid(ObjectDisplayEditorWindow)),
            "Object Displays",
            [this]() { OpenEditorWindow(std::type_index(typeid(ObjectDisplayEditorWindow))); },
            static_cast<int>(m_project.objectDisplays.count())
        });
        
        // Add editors to the world category
        worldCategory.editors.push_back({
            std::type_index(typeid(MapEditorWindow)),
            "Maps",
            [this]() { OpenEditorWindow(std::type_index(typeid(MapEditorWindow))); },
            static_cast<int>(m_project.maps.count())
        });
        
        worldCategory.editors.push_back({
            std::type_index(typeid(ObjectEditorWindow)),
            "Objects",
            [this]() { OpenEditorWindow(std::type_index(typeid(ObjectEditorWindow))); },
            static_cast<int>(m_project.objects.count())
        });
        
        worldCategory.editors.push_back({
            std::type_index(typeid(ZoneEditorWindow)),
            "Zones",
            [this]() { OpenEditorWindow(std::type_index(typeid(ZoneEditorWindow))); },
            static_cast<int>(m_project.zones.count())
        });
        
        // Add editors to the misc category
        miscCategory.editors.push_back({
            std::type_index(typeid(UnitLootEditorWindow)),
            "Unit Loot",
            [this]() { OpenEditorWindow(std::type_index(typeid(UnitLootEditorWindow))); },
            static_cast<int>(m_project.unitLoot.count())
        });
        
        miscCategory.editors.push_back({
            std::type_index(typeid(TrainerEditorWindow)),
            "Trainers",
            [this]() { OpenEditorWindow(std::type_index(typeid(TrainerEditorWindow))); },
            static_cast<int>(m_project.trainers.count())
        });
        
        miscCategory.editors.push_back({
            std::type_index(typeid(VendorEditorWindow)),
            "Vendors",
            [this]() { OpenEditorWindow(std::type_index(typeid(VendorEditorWindow))); },
            static_cast<int>(m_project.vendors.count())
        });

        miscCategory.editors.push_back({
            std::type_index(typeid(GossipEditorWindow)),
            "Gossip",
            [this]() { OpenEditorWindow(std::type_index(typeid(GossipEditorWindow))); },
            static_cast<int>(m_project.gossipMenus.count())
            });

        miscCategory.editors.push_back({
            std::type_index(typeid(CombatSettingsEditorWindow)),
            "Combat Settings",
            [this]() { OpenEditorWindow(std::type_index(typeid(CombatSettingsEditorWindow))); },
            0
        });

        miscCategory.editors.push_back({
            std::type_index(typeid(VariableEditorWindow)),
            "Variables",
            [this]() { OpenEditorWindow(std::type_index(typeid(VariableEditorWindow))); },
            static_cast<int>(m_project.variables.count())
        });

        miscCategory.editors.push_back({
            std::type_index(typeid(ChatChannelEditorWindow)),
            "Chat Channels",
            [this]() { OpenEditorWindow(std::type_index(typeid(ChatChannelEditorWindow))); },
            static_cast<int>(m_project.chatChannels.count())
        });
        
        // Add all categories to the main list
        m_categories.push_back(std::move(gameplayCategory));
        m_categories.push_back(std::move(characterCategory));
        m_categories.push_back(std::move(visualsCategory));
        m_categories.push_back(std::move(worldCategory));
        m_categories.push_back(std::move(miscCategory));
    }
}