// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "data_navigator_window.h"

#include <imgui.h>
#include <cctype>
#include <algorithm>

#include "base/macros.h"
#include "editor_host.h"

// Include all editor window headers for type information
#include "spell_editor_window.h"
#include "quest_editor_window.h"
#include "item_editor_window.h"
#include "range_type_editor_window.h"
#include "creature_editor_window.h"
#include "class_editor_window.h"
#include "race_editor_window.h"
#include "faction_editor_window.h"
#include "faction_template_editor_window.h"
#include "model_editor_window.h"
#include "item_display_editor_window.h"
#include "object_display_editor_window.h"
#include "map_editor_window.h"
#include "object_editor_window.h"
#include "zone_editor_window.h"
#include "unit_loot_editor_window.h"
#include "trainer_editor_window.h"
#include "vendor_editor_window.h"

namespace mmo
{
    DataNavigatorWindow::DataNavigatorWindow(const String& name, proto::Project& project, EditorHost& host)
        : EditorWindowBase(name)
        , m_project(project)
        , m_host(host)
    {
        EditorWindowBase::SetVisible(true);
        m_hasToolbarButton = true;
        m_toolbarButtonText = "Data Navigator";
        
        // Initialize the categories and editors list
        InitializeCategories();
    }

    bool DataNavigatorWindow::Draw()
    {
        if (ImGui::Begin(m_name.c_str(), &m_visible))
        {
            // Draw a search filter at the top
            static char searchBuffer[256] = "";
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##search", "Search...", searchBuffer, IM_ARRAYSIZE(searchBuffer));
            
            const String searchText = searchBuffer;
            const bool hasSearchFilter = !searchText.empty();
            
            // Calculate if we need to show the category name based on filtering
            bool showCategoryHeader = !hasSearchFilter;
            
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
                    ImGuiTreeNodeFlags categoryFlags = ImGuiTreeNodeFlags_DefaultOpen;
                    category.isOpen = ImGui::CollapsingHeader(category.name.c_str(), categoryFlags);
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
                        
                        // Display the editor entry with its item count
                        String displayText = editor.displayName;
                        if (editor.count > 0)
                        {
                            displayText += " (" + std::to_string(editor.count) + ")";
                        }
                        
                        if (ImGui::Selectable(displayText.c_str()))
                        {
                            // Call the callback to open this editor
                            if (editor.openCallback)
                            {
                                editor.openCallback();
                            }
                        }
                        
                        ImGui::PopID();
                        
                        // Remove indentation
                        if (showCategoryHeader)
                        {
                            ImGui::Unindent();
                        }
                    }
                }
            }
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
            std::type_index(typeid(RangeTypeEditorWindow)),
            "Spell Range Types",
            [this]() { OpenEditorWindow(std::type_index(typeid(RangeTypeEditorWindow))); },
            static_cast<int>(m_project.ranges.count())
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
            std::type_index(typeid(RaceEditorWindow)),
            "Races",
            [this]() { OpenEditorWindow(std::type_index(typeid(RaceEditorWindow))); },
            static_cast<int>(m_project.races.count())
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
        
        // Add all categories to the main list
        m_categories.push_back(std::move(gameplayCategory));
        m_categories.push_back(std::move(characterCategory));
        m_categories.push_back(std::move(visualsCategory));
        m_categories.push_back(std::move(worldCategory));
        m_categories.push_back(std::move(miscCategory));
    }
}