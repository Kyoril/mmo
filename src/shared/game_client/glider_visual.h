// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "glider_fold_state.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <array>

namespace mmo
{
	class Scene;
	class Entity;
	class TagPoint;
	class ParticleSystem;

	/// @brief The visible glider of one unit: the glider mesh on the unit's back, its unfold
	///	animation, the wing-tip wind trails and the opening burst. Owned by GameUnitC, driven
	///	by the unit's Gliding movement flag each frame. Main thread only.
	/// @remark A host entity without the attach bone (creatures, rigs without the back bone) or a
	///	missing glider mesh shows nothing, but the fold state and its events still run, so sounds
	///	hung on the events keep working.
	class GliderVisual final : public NonCopyable
	{
	public:
		/// Mesh of the glider.
		static constexpr const char* MeshFile = "Models/Gear/Glider/Glider.hmsh";
		/// Wind trail emitted from both wing tips while the glider is fully open.
		static constexpr const char* TrailParticles = "Particles/GliderTrail.hpar";
		/// One-shot burst when the glider opens.
		static constexpr const char* OpenParticles = "Particles/GliderOpen.hpar";
		/// Bone the glider is attached to.
		static constexpr const char* AttachBone = "spine_03";

	public:
		/// @brief Creates the glider objects for a host entity (folded and hidden).
		/// @param scene Scene owning the host entity.
		/// @param host Unit entity the glider is attached to. Must outlive this object.
		/// @param guid Guid of the unit, used for unique scene object names.
		GliderVisual(Scene& scene, Entity& host, uint64 guid);

		/// @brief Detaches and destroys every scene object this glider created.
		~GliderVisual();

	public:
		/// @brief Advances the unfold animation and shows, scales and hides the glider.
		/// @param gliding Whether the unit glides this frame.
		/// @param deltaTime Frame time in seconds.
		/// @return The edge reported by the fold state this frame.
		glider_event::Type Update(bool gliding, float deltaTime);

		/// @brief Whether the glider is shown (between Opened and Closed).
		[[nodiscard]] bool IsVisible() const { return m_fold.IsVisible(); }

		/// @brief Whether the glider mesh is attached to the host (false: nothing is drawn).
		[[nodiscard]] bool HasMesh() const { return m_glider != nullptr; }

	private:
		ParticleSystem* CreateEmitter(const char* file, const char* suffix);

		void DestroyEmitter(ParticleSystem*& emitter);

	private:
		Scene& m_scene;
		Entity& m_host;
		uint64 m_guid;
		GliderFoldState m_fold;
		Entity* m_glider{nullptr};
		TagPoint* m_gliderAttachment{nullptr};
		std::array<ParticleSystem*, 2> m_trails{};
		ParticleSystem* m_openBurst{nullptr};
		bool m_trailsPlaying{false};
	};
}
