#include "sky_component.h"

#include "global_shader_parameters.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "game/constants.h"
#include "log/default_log_levels.h"
#include "scene_graph/atmosphere_settings.h"

namespace mmo
{
    SkyComponent::SkyComponent(Scene& scene, GameTimeComponent* gameTime)
        : m_scene(scene)
        , m_gameTime(gameTime)
        , m_cloudsNode(nullptr)
        , m_cloudsEntity(nullptr)
        , m_sunLight(nullptr)
        , m_sunLightNode(nullptr)
    {
        // Create own game time component if not provided
        if (!m_gameTime)
        {
            m_ownedGameTime = std::make_unique<GameTimeComponent>();
            m_gameTime = m_ownedGameTime.get();
        }

        // Start from the built-in Default so the first frame is never black, even before a caller
        // applies its first environment state.
        m_environment = EvaluateEnvironment(*EnvironmentProfile::GetDefault(), GetNormalizedTimeOfDay());

        // Create sky entity
        m_cloudsEntity = m_scene.CreateEntity("Clouds", "Models/SkySphere.hmsh");
        m_cloudsEntity->SetRenderQueueGroup(SkiesEarly);
        m_cloudsEntity->SetQueryFlags(0);
        m_cloudsNode = &m_scene.CreateSceneNode("Clouds");
        m_cloudsNode->AttachObject(*m_cloudsEntity);
        m_cloudsNode->SetScale(Vector3::UnitScale * 40.0f);
        m_scene.GetRootSceneNode().AddChild(*m_cloudsNode);

        ASSERT(m_cloudsEntity->GetNumSubEntities() > 0);
        m_skyMatInst = std::make_shared<MaterialInstance>("__Sky__", m_cloudsEntity->GetSubEntity(0)->GetMaterial());
        m_cloudsEntity->GetSubEntity(0)->SetMaterial(m_skyMatInst);

        // Setup sun light
        m_sunLightNode = m_scene.GetRootSceneNode().CreateChildSceneNode("SunLightNode");
        m_sunLight = &m_scene.CreateLight("SunLight", LightType::Directional);
        m_sunLightNode->AttachObject(*m_sunLight);
        m_sunLight->SetDirection({ -0.5f, -1.0f, -0.3f });
        m_sunLight->SetIntensity(1.0f);
        m_sunLight->SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        m_sunLight->SetCastShadows(true);
        m_sunLight->SetShadowFarDistance(75.0f);

        // Initial update of lighting based on current time
        UpdateLighting(GetNormalizedTimeOfDay());
    }

    SkyComponent::~SkyComponent()
    {
        // Scene will handle cleanup of entities and lights
    }

    void SkyComponent::Update(float deltaSeconds, GameTime timestamp)
    {
        // Update game time
        m_gameTime->Update(timestamp);

        // Rotate clouds slightly for visual effect
        if (m_cloudsNode)
        {
            m_cloudsNode->Yaw(Radian(deltaSeconds * 0.0025f), TransformSpace::World);
        }
    }

    void SkyComponent::SetPosition(const Vector3& position)
    {
        if (m_cloudsNode)
        {
            m_cloudsNode->SetPosition(position);
        }
    }

    void SkyComponent::ApplyEnvironment(const EnvironmentState& state)
    {
        m_environment = state;
        UpdateLighting(GetNormalizedTimeOfDay());
    }

    float SkyComponent::GetNormalizedTimeOfDay() const
    {
        return m_gameTime->GetNormalizedTimeOfDay();
    }

    void SkyComponent::SetNormalizedTimeOfDay(float normalizedTime)
    {
        // Ensure normalized time is between 0 and 1
        normalizedTime = std::max(0.0f, std::min(normalizedTime, 1.0f));
        
        // Calculate game time in milliseconds
        GameTime gameTime = static_cast<GameTime>(normalizedTime * constants::OneDay);
        m_gameTime->SetTime(gameTime);
        
        // Update lighting
        UpdateLighting(normalizedTime);
    }

    uint32 SkyComponent::GetHour() const
    {
        return m_gameTime->GetHour();
    }

    uint32 SkyComponent::GetMinute() const
    {
        return m_gameTime->GetMinute();
    }

    uint32 SkyComponent::GetSecond() const
    {
        return m_gameTime->GetSecond();
    }

    void SkyComponent::SetTime(uint32 hour, uint32 minute, uint32 second)
    {
        // Clamp values to valid ranges
        hour = std::min(hour, 23U);
        minute = std::min(minute, 59U);
        second = std::min(second, 59U);
        
        // Calculate total milliseconds
        GameTime gameTime = (hour * constants::OneHour) + 
                           (minute * constants::OneMinute) + 
                           (second * constants::OneSecond);
        
        m_gameTime->SetTime(gameTime);
        
        // Update lighting
        UpdateLighting(GetNormalizedTimeOfDay());
    }

    float SkyComponent::GetTimeSpeed() const
    {
        return m_gameTime->GetTimeSpeed();
    }

    void SkyComponent::SetTimeSpeed(float speed)
    {
        m_gameTime->SetTimeSpeed(speed);
    }

    std::string SkyComponent::GetTimeString() const
    {
        return m_gameTime->GetTimeString();
    }

    GameTimeComponent* SkyComponent::GetGameTimeComponent()
    {
        return m_gameTime;
    }
	
	void SkyComponent::UpdateLighting(float normalizedTime)
    {
        if (!m_sunLight || !m_sunLightNode)
            return;

        // Define the time points for the day/night cycle
        // Day cycle runs from m_transitionStart to m_transitionEnd (day is in the middle)
        // Night cycle runs from m_transitionEnd to m_transitionStart (with potential wrap around at 1.0/0.0)
        
        // Calculate sun/moon blend factors
        float blendSun = 0.0f;
        if (normalizedTime >= m_dayStart && normalizedTime <= m_dayEnd)
        {
            blendSun = 1.0f; // Full sun during day
        }
        else if (normalizedTime >= m_transitionStart && normalizedTime < m_dayStart)
        {
            // Dawn transition (increasing sun)
            blendSun = (normalizedTime - m_transitionStart) / (m_dayStart - m_transitionStart);
        }
        else if (normalizedTime > m_dayEnd && normalizedTime <= m_transitionEnd)
        {
            // Dusk transition (decreasing sun)
            blendSun = 1.0f - (normalizedTime - m_dayEnd) / (m_transitionEnd - m_dayEnd);
        }

        float blendMoon = 1.0f - blendSun;
        
        // Calculate a unified arc position for both sun and moon
        // Both celestial bodies follow the same path across the sky
        float timeInArc;
        if (normalizedTime >= m_transitionStart && normalizedTime <= m_transitionEnd)
        {
            // During daytime arc (sun is visible)
            timeInArc = (normalizedTime - m_transitionStart) / (m_transitionEnd - m_transitionStart);
        }
        else
        {
            // During nighttime arc (moon is visible)
            // We need to map the night time to continue the arc smoothly
            float nightDuration = m_transitionStart + (1.0f - m_transitionEnd);
            
            if (normalizedTime > m_transitionEnd)
            {
                // From end of day to midnight (continuing the arc)
                float progress = (normalizedTime - m_transitionEnd) / (1.0f - m_transitionEnd);
                timeInArc = 1.0f + (progress * (m_transitionStart / nightDuration));
            }
            else // normalizedTime < m_transitionStart
            {
                // From midnight to dawn (finishing the arc)
                float progress = normalizedTime / m_transitionStart;
                timeInArc = 1.0f + ((1.0f - m_transitionEnd) / nightDuration) + (progress * (m_transitionStart / nightDuration));
            }
            
            // Normalize to 0.0 - 1.0 range by wrapping around
            timeInArc = timeInArc - std::floor(timeInArc);
        }        
        // Convert the time in arc to an angle in radians (from -90° to +90°)
        float angleRadians = m_arcMin + timeInArc * (m_arcMax - m_arcMin);
        
        // Build light direction
        const float x = -std::sin(angleRadians);
        const float y = -std::cos(angleRadians); // always <= 0
        const float z = -0.3f;
        
        Vector3 lightDir = Vector3(x, y, z).NormalizedCopy();

        // Light colour & intensity from the environment; the clock decides how much is sun vs moon.
        const Vector4 sunColor(m_environment.sunColor.x, m_environment.sunColor.y, m_environment.sunColor.z, 1.0f);
        const Vector4 moonColor(m_environment.moonColor.x, m_environment.moonColor.y, m_environment.moonColor.z, 1.0f);

        Vector4 blendedColor = sunColor * blendSun + moonColor * blendMoon;
        float blendedIntensity = m_environment.sunIntensity * blendSun + m_environment.moonIntensity * blendMoon;

        // Apply to shared light
        m_sunLight->SetDirection(lightDir);
        m_sunLight->SetColor(blendedColor);
        m_sunLight->SetIntensity(blendedIntensity);

        // Update light direction in material
        m_skyMatInst->SetVectorParameter("LightDirection", Vector4(lightDir.x, lightDir.y, lightDir.z, 0.0f));
        m_skyMatInst->SetScalarParameter("SunHeight", blendMoon);

        const Vector4& horizonColor = m_environment.skyHorizon;
        const Vector4& zenithColor = m_environment.skyZenith;
        m_skyMatInst->SetVectorParameter("HorizonColor", horizonColor);
        m_skyMatInst->SetVectorParameter("ZenithColor", zenithColor);
        m_skyMatInst->SetVectorParameter("CloudColor", m_environment.clouds);

        m_scene.SetAtmosphereParameters(m_environment.atmosphere);
        m_scene.SetAtmosphereTimeOfDay(m_environment.timeOfDay);
        m_scene.SetAmbientColor(m_environment.ambient);

        // Register the sun as the primary directional light so that forward-rendered
        // translucent surfaces (water, glass …) pick up the correct sun direction and colour
        // from the camera constant buffer instead of using hardcoded shader fallbacks.
        m_scene.SetPrimaryDirectionalLight(m_sunLight);

        GlobalShaderParameters::Get().SetVector("SkyHorizonColor", horizonColor);
        GlobalShaderParameters::Get().SetVector("SkyZenithColor", zenithColor);

        // Direction pointing TOWARD the light source, matching the convention the forward camera
        // constant buffer uses (Scene negates the raw light direction for the same reason).
        // Materials reading this must not negate it again.
        Vector3 towardSun = -lightDir;
        towardSun.Normalize();
        GlobalShaderParameters::Get().SetVector("SunDirection",
            Vector4(towardSun.x, towardSun.y, towardSun.z, 0.0f));

        // rgb is the blended sun/moon colour, a carries intensity, so a material can reconstruct
        // the full contribution from a single parameter.
        GlobalShaderParameters::Get().SetVector("SunColor",
            Vector4(blendedColor.x, blendedColor.y, blendedColor.z, blendedIntensity));
    }
}
