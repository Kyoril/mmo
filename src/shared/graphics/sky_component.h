#pragma once

#include "base/typedefs.h"
#include "graphics/color_curve.h"
#include "game/game_time_component.h"
#include "scene_graph/light.h"
#include "scene_graph/scene.h"
#include "graphics/material_instance.h"

namespace mmo
{
    /**
     * @class SkyComponent
     * @brief Manages the sky entity, lighting and day/night cycle in the game world.
     * 
     * This component controls the sky dome appearance, directional light properties,
     * and handles transitions between day and night based on game time. It can be used
     * both by the game client and editor tools.
     */
    class SkyComponent final
    {
    public:
        /**
         * @brief Construct a new SkyComponent.
         * 
         * @param scene Scene to which the sky and lights will be added
         * @param gameTime Optional GameTimeComponent to control time of day (if null, uses internal time)
         */
        explicit SkyComponent(Scene& scene, GameTimeComponent* gameTime = nullptr);
        
        /**
         * @brief Destructor.
         */
        ~SkyComponent();

        /**
         * @brief Updates the sky and lighting based on current time.
         * 
         * @param deltaSeconds The time passed since the last update
         * @param timestamp Current game timestamp
         */
        void Update(float deltaSeconds, GameTime timestamp);

        /**
         * @brief Sets the position of the sky dome to follow the camera/player.
         * 
         * @param position The new position for the sky dome
         */
        void SetPosition(const Vector3& position);

        /**
         * @brief Gets the current normalized time of day (0.0-1.0).
         * 
         * @return float Normalized time of day where 0.0 = midnight, 0.5 = noon
         */
        float GetNormalizedTimeOfDay() const;
        
        /**
         * @brief Sets the normalized time of day directly.
         * 
         * Useful for the editor to preview different lighting conditions.
         * 
         * @param normalizedTime Normalized time value (0.0-1.0)
         */
        void SetNormalizedTimeOfDay(float normalizedTime);

        /**
         * @brief Gets the current hour.
         * 
         * @return uint32 Current hour (0-23)
         */
        uint32 GetHour() const;

        /**
         * @brief Gets the current minute.
         * 
         * @return uint32 Current minute (0-59)
         */
        uint32 GetMinute() const;

        /**
         * @brief Gets the current second.
         * 
         * @return uint32 Current second (0-59)
         */
        uint32 GetSecond() const;

        /**
         * @brief Sets the time to a specific hour, minute, and second.
         * 
         * @param hour Hour value (0-23)
         * @param minute Minute value (0-59)
         * @param second Second value (0-59)
         */
        void SetTime(uint32 hour, uint32 minute, uint32 second);

        /**
         * @brief Gets the time speed multiplier.
         * 
         * @return float Current time speed multiplier
         */
        float GetTimeSpeed() const;

        /**
         * @brief Sets the time speed multiplier.
         * 
         * @param speed New time speed multiplier (0 = paused, 1 = normal, etc.)
         */
        void SetTimeSpeed(float speed);

        /**
         * @brief Gets a formatted time string (HH:MM:SS).
         * 
         * @return std::string Formatted time string
         */
        std::string GetTimeString() const;

        /**
         * @brief Gets pointer to the internal GameTimeComponent.
         * 
         * @return GameTimeComponent* Pointer to the game time component
         */
        GameTimeComponent* GetGameTimeComponent();

        /**
         * @brief Gets the sun light.
         * 
         * @return Light* Pointer to the sun light
         */
        Light* GetSunLight() const { return m_sunLight; }

        /**
         * @brief Gets the sun light node.
         * 
         * @return SceneNode* Pointer to the sun light node
         */
        SceneNode* GetSunLightNode() const { return m_sunLightNode; }

    private:
        /**
         * @brief Loads the color curves from asset files.
         */
        void LoadColorCurves();

        /**
         * @brief Updates all lighting based on the current time.
         * 
         * @param normalizedTime Normalized time of day (0.0-1.0)
         */
        void UpdateLighting(float normalizedTime);

    private:
        Scene& m_scene;                                 ///< Reference to the scene
        std::unique_ptr<GameTimeComponent> m_ownedGameTime;  ///< Owned game time component (if not provided)
        GameTimeComponent* m_gameTime;                  ///< Game time component (owned or external)

        SceneNode* m_cloudsNode;                       ///< Sky dome scene node
        Entity* m_cloudsEntity;                        ///< Sky dome entity
        std::shared_ptr<MaterialInstance> m_skyMatInst;              ///< Sky material instance

        Light* m_sunLight;                             ///< Sun directional light
        SceneNode* m_sunLightNode;                     ///< Sun light scene node

        std::unique_ptr<ColorCurve> m_horizonColorCurve; ///< Horizon color curve over time
        std::unique_ptr<ColorCurve> m_zenithColorCurve;  ///< Zenith color curve over time
        std::unique_ptr<ColorCurve> m_ambientColorCurve;  ///< Ambient color curve over time
        std::unique_ptr<ColorCurve> m_cloudColorCurve;  ///< Cloud color curve over time

        // Configuration values
        const float m_arcMin = -Pi / 2.0f;             ///< Sunrise/sunset horizon
        const float m_arcMax = Pi / 2.0f;              ///< Overhead noon
        const float m_transitionStart = 0.20f;         ///< Dawn transition start time
        const float m_dayStart = 0.30f;                ///< Full daylight start time
        const float m_dayEnd = 0.70f;                  ///< Full daylight end time
        const float m_transitionEnd = 0.80f;           ///< Dusk transition end time
    };
}
