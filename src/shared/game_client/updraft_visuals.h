// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "game/traversal.h"

#include <vector>

namespace mmo
{
	class Scene;
	class SceneNode;
	class ParticleSystem;

	/// @brief The visible updraft columns of the current map: one looping particle column per
	///	updraft volume, rising from the volume's bottom across its footprint. Main thread only.
	class UpdraftVisuals final : public NonCopyable
	{
	public:
		/// Particle system of one column; box emitters take the volume's footprint.
		static constexpr const char* ColumnParticles = "Particles/Updraft.hpar";

	public:
		explicit UpdraftVisuals(Scene& scene);

		/// @brief Destroys all columns.
		~UpdraftVisuals();

	public:
		/// @brief Replaces the shown columns with one per volume (strength > 0 only).
		void Show(const std::vector<UpdraftVolume>& volumes);

		/// @brief Destroys all columns.
		void Clear();

		/// @brief Gets the number of columns shown.
		[[nodiscard]] size_t GetCount() const { return m_columns.size(); }

	private:
		struct Column
		{
			SceneNode* node{nullptr};
			ParticleSystem* emitter{nullptr};
		};

		Scene& m_scene;
		std::vector<Column> m_columns;
	};
}
