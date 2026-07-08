// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "localization_io.h"

#include "base/localization.h"
#include "proto_data/project.h"
#include "proto_data/trigger_helper.h"
#include "log/default_log_levels.h"

#include "nlohmann/json.hpp"

#include <array>
#include <fstream>

namespace mmo
{
	namespace
	{
		using json = nlohmann::json;
		using LocRepeated = google::protobuf::RepeatedPtrField<proto::LocalizedString>;

		/// Version marker written into the translation file so the importer can validate it.
		constexpr const char* s_translationFormat = "mmo-translations";
		constexpr int s_translationVersion = 1;

		/// Target locales that can be translated (enUS is the authoritative base and is never exported/imported).
		constexpr std::array<LocaleIndex, 4> s_targetLocales = {
			LocaleIndex::deDE, LocaleIndex::enGB, LocaleIndex::frFR, LocaleIndex::ruRU
		};

		/// Appends one localizable field to the export array. Entries with an empty source are skipped.
		/// @param sub Optional secondary id (gossip option id / quest requirement index); 0 means "none".
		void ExportField(json& entries, const char* type, const uint32 id, const uint32 sub, const char* field,
			const std::string& base, const LocRepeated& loc)
		{
			if (base.empty())
			{
				// Nothing to translate.
				return;
			}

			json entry = json::object();
			entry["type"] = type;
			entry["id"] = id;
			if (sub != 0)
			{
				entry["sub"] = sub;
			}
			entry["field"] = field;
			entry["source"] = base;

			// Pre-populate every target locale so blank slots act as a to-translate worklist.
			json translations = json::object();
			for (const LocaleIndex locale : s_targetLocales)
			{
				translations[LocaleCode(locale)] = "";
			}

			// Fill in any existing overrides.
			for (const auto& override : loc)
			{
				const LocaleIndex locale = LocaleIndexFromValue(override.locale());
				if (locale != LocaleIndex::enUS)
				{
					translations[LocaleCode(locale)] = override.value();
				}
			}

			entry["translations"] = std::move(translations);
			entries.push_back(std::move(entry));
		}

		/// Returns true if a trigger action type carries user-facing text worth localizing.
		/// Only Say / Yell / Emote broadcast their texts[0] to players; other text-bearing
		/// actions (e.g. SetVariable) use it as an internal key/value that must not be translated.
		bool IsLocalizableTextAction(const uint32 actionType)
		{
			return actionType == trigger_actions::Say
				|| actionType == trigger_actions::Yell
				|| actionType == trigger_actions::Emote
				|| actionType == trigger_actions::BroadcastMessage;
		}

		/// Applies the non-empty translation values from a JSON translations map to a per-locale override array.
		/// The base English field is never touched. Missing or empty values are left as-is (additive/updatable).
		void ApplyTranslations(LocRepeated* loc, const json& translations, size_t& appliedCount)
		{
			if (!translations.is_object())
			{
				return;
			}

			for (auto it = translations.begin(); it != translations.end(); ++it)
			{
				if (!it.value().is_string())
				{
					continue;
				}

				const std::string value = it.value().get<std::string>();
				if (value.empty())
				{
					continue;
				}

				const LocaleIndex locale = LocaleIndexFromString(it.key());
				if (locale == LocaleIndex::enUS)
				{
					// enUS is the authoritative base field and is never stored as an override.
					continue;
				}

				FindOrAddLoc(loc, locale)->set_value(value);
				++appliedCount;
			}
		}
	}

	bool ExportTranslations(const proto::Project& project, const String& path, String& outError, size_t& outEntryCount)
	{
		json entries = json::array();

		// Spells
		const auto& spells = project.spells.getTemplates();
		for (int i = 0; i < spells.entry_size(); ++i)
		{
			const auto& e = spells.entry(i);
			ExportField(entries, "spell", e.id(), 0, "name", e.name(), e.name_loc());
			ExportField(entries, "spell", e.id(), 0, "description", e.description(), e.description_loc());
			ExportField(entries, "spell", e.id(), 0, "auratext", e.auratext(), e.auratext_loc());
		}

		// Items
		const auto& items = project.items.getTemplates();
		for (int i = 0; i < items.entry_size(); ++i)
		{
			const auto& e = items.entry(i);
			ExportField(entries, "item", e.id(), 0, "name", e.name(), e.name_loc());
			ExportField(entries, "item", e.id(), 0, "description", e.description(), e.description_loc());
		}

		// Units (creatures)
		const auto& units = project.units.getTemplates();
		for (int i = 0; i < units.entry_size(); ++i)
		{
			const auto& e = units.entry(i);
			ExportField(entries, "unit", e.id(), 0, "name", e.name(), e.name_loc());
			ExportField(entries, "unit", e.id(), 0, "subname", e.subname(), e.subname_loc());
		}

		// Objects
		const auto& objects = project.objects.getTemplates();
		for (int i = 0; i < objects.entry_size(); ++i)
		{
			const auto& e = objects.entry(i);
			ExportField(entries, "object", e.id(), 0, "name", e.name(), e.name_loc());
			ExportField(entries, "object", e.id(), 0, "caption", e.caption(), e.caption_loc());
		}

		// Quests (top-level texts + per-requirement objective text)
		const auto& quests = project.quests.getTemplates();
		for (int i = 0; i < quests.entry_size(); ++i)
		{
			const auto& e = quests.entry(i);
			ExportField(entries, "quest", e.id(), 0, "name", e.name(), e.name_loc());
			ExportField(entries, "quest", e.id(), 0, "detailstext", e.detailstext(), e.detailstext_loc());
			ExportField(entries, "quest", e.id(), 0, "objectivestext", e.objectivestext(), e.objectivestext_loc());
			ExportField(entries, "quest", e.id(), 0, "offerrewardtext", e.offerrewardtext(), e.offerrewardtext_loc());
			ExportField(entries, "quest", e.id(), 0, "requestitemstext", e.requestitemstext(), e.requestitemstext_loc());
			ExportField(entries, "quest", e.id(), 0, "endtext", e.endtext(), e.endtext_loc());

			for (int r = 0; r < e.requirements_size(); ++r)
			{
				const auto& req = e.requirements(r);
				// 'sub' is the 1-based requirement index (0 is reserved for "no sub").
				ExportField(entries, "quest_requirement", e.id(), static_cast<uint32>(r + 1), "text", req.text(), req.text_loc());
			}
		}

		// Gossip menus (menu body text + each option's reply text)
		const auto& gossipMenus = project.gossipMenus.getTemplates();
		for (int i = 0; i < gossipMenus.entry_size(); ++i)
		{
			const auto& e = gossipMenus.entry(i);
			ExportField(entries, "gossip_menu", e.id(), 0, "text", e.text(), e.text_loc());

			for (int o = 0; o < e.options_size(); ++o)
			{
				const auto& option = e.options(o);
				ExportField(entries, "gossip_option", e.id(), option.id(), "text", option.text(), option.text_loc());
			}
		}

		// Zones
		const auto& zones = project.zones.getTemplates();
		for (int i = 0; i < zones.entry_size(); ++i)
		{
			const auto& e = zones.entry(i);
			ExportField(entries, "zone", e.id(), 0, "name", e.name(), e.name_loc());
		}

		// Triggers (per-action Say / Yell / Emote text). 'sub' is the 1-based action index.
		const auto& triggers = project.triggers.getTemplates();
		for (int i = 0; i < triggers.entry_size(); ++i)
		{
			const auto& e = triggers.entry(i);
			for (int a = 0; a < e.actions_size(); ++a)
			{
				const auto& action = e.actions(a);
				if (!IsLocalizableTextAction(action.action()) || action.texts_size() == 0)
				{
					continue;
				}

				ExportField(entries, "trigger", e.id(), static_cast<uint32>(a + 1), "text", action.texts(0), action.texts_loc());
			}
		}

		json doc = json::object();
		doc["format"] = s_translationFormat;
		doc["version"] = s_translationVersion;
		doc["sourceLocale"] = LocaleCode(LocaleIndex::enUS);
		doc["entries"] = std::move(entries);

		std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!file)
		{
			outError = "Could not open '" + path + "' for writing.";
			return false;
		}

		file << doc.dump(2);
		file.close();
		if (!file)
		{
			outError = "An error occurred while writing '" + path + "'.";
			return false;
		}

		outEntryCount = doc["entries"].size();
		return true;
	}

	bool ImportTranslations(proto::Project& project, const String& path, String& outError, size_t& outAppliedCount)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file)
		{
			outError = "Could not open '" + path + "' for reading.";
			return false;
		}

		json doc;
		try
		{
			file >> doc;
		}
		catch (const json::exception& e)
		{
			outError = std::string("Failed to parse translation file: ") + e.what();
			return false;
		}

		if (!doc.is_object() || !doc.contains("format") || doc["format"] != s_translationFormat)
		{
			outError = "File is not a valid mmo translation file.";
			return false;
		}

		if (!doc.contains("entries") || !doc["entries"].is_array())
		{
			outError = "Translation file is missing an 'entries' array.";
			return false;
		}

		size_t applied = 0;
		size_t skipped = 0;

		for (const json& entry : doc["entries"])
		{
			if (!entry.is_object() || !entry.contains("type") || !entry.contains("id") ||
				!entry.contains("field") || !entry.contains("translations"))
			{
				++skipped;
				continue;
			}

			const std::string type = entry["type"].get<std::string>();
			const uint32 id = entry["id"].get<uint32>();
			const uint32 sub = entry.contains("sub") ? entry["sub"].get<uint32>() : 0;
			const std::string field = entry["field"].get<std::string>();
			const json& translations = entry["translations"];

			// Resolve the mutable per-locale override array for this (type, id, sub, field).
			LocRepeated* loc = nullptr;

			if (type == "spell")
			{
				if (auto* e = project.spells.getById(id))
				{
					if (field == "name") { loc = e->mutable_name_loc(); }
					else if (field == "description") { loc = e->mutable_description_loc(); }
					else if (field == "auratext") { loc = e->mutable_auratext_loc(); }
				}
			}
			else if (type == "item")
			{
				if (auto* e = project.items.getById(id))
				{
					if (field == "name") { loc = e->mutable_name_loc(); }
					else if (field == "description") { loc = e->mutable_description_loc(); }
				}
			}
			else if (type == "unit")
			{
				if (auto* e = project.units.getById(id))
				{
					if (field == "name") { loc = e->mutable_name_loc(); }
					else if (field == "subname") { loc = e->mutable_subname_loc(); }
				}
			}
			else if (type == "object")
			{
				if (auto* e = project.objects.getById(id))
				{
					if (field == "name") { loc = e->mutable_name_loc(); }
					else if (field == "caption") { loc = e->mutable_caption_loc(); }
				}
			}
			else if (type == "quest")
			{
				if (auto* e = project.quests.getById(id))
				{
					if (field == "name") { loc = e->mutable_name_loc(); }
					else if (field == "detailstext") { loc = e->mutable_detailstext_loc(); }
					else if (field == "objectivestext") { loc = e->mutable_objectivestext_loc(); }
					else if (field == "offerrewardtext") { loc = e->mutable_offerrewardtext_loc(); }
					else if (field == "requestitemstext") { loc = e->mutable_requestitemstext_loc(); }
					else if (field == "endtext") { loc = e->mutable_endtext_loc(); }
				}
			}
			else if (type == "quest_requirement")
			{
				if (auto* e = project.quests.getById(id))
				{
					// 'sub' is the 1-based requirement index.
					const int index = static_cast<int>(sub) - 1;
					if (index >= 0 && index < e->requirements_size() && field == "text")
					{
						loc = e->mutable_requirements(index)->mutable_text_loc();
					}
				}
			}
			else if (type == "gossip_menu")
			{
				if (auto* e = project.gossipMenus.getById(id))
				{
					if (field == "text") { loc = e->mutable_text_loc(); }
				}
			}
			else if (type == "gossip_option")
			{
				if (auto* e = project.gossipMenus.getById(id))
				{
					for (int o = 0; o < e->options_size(); ++o)
					{
						if (e->options(o).id() == sub)
						{
							if (field == "text") { loc = e->mutable_options(o)->mutable_text_loc(); }
							break;
						}
					}
				}
			}
			else if (type == "zone")
			{
				if (auto* e = project.zones.getById(id))
				{
					if (field == "name") { loc = e->mutable_name_loc(); }
				}
			}
			else if (type == "trigger")
			{
				if (auto* e = project.triggers.getById(id))
				{
					// 'sub' is the 1-based action index.
					const int index = static_cast<int>(sub) - 1;
					if (index >= 0 && index < e->actions_size() && field == "text")
					{
						loc = e->mutable_actions(index)->mutable_texts_loc();
					}
				}
			}

			if (loc == nullptr)
			{
				// Unknown type/field or a referenced entry no longer exists - skip gracefully.
				++skipped;
				continue;
			}

			ApplyTranslations(loc, translations, applied);
		}

		if (skipped > 0)
		{
			WLOG("Translation import skipped " << skipped << " entr" << (skipped == 1 ? "y" : "ies") << " (unknown type/field or missing entry).");
		}

		outAppliedCount = applied;
		return true;
	}
}
