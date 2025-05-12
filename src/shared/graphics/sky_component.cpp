#include "sky_component.h"

#include "assets/asset_registry.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "graphics/color_curve.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene_node.h"
#include "game/constants.h"
#include "log/default_log_levels.h"

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

        // Load color curves for sky
        LoadColorCurves();

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

    void SkyComponent::LoadColorCurves()
    {
        m_horizonColorCurve = std::make_unique<ColorCurve>();
        if (const auto file = AssetRegistry::OpenFile("Models/HorizonColor.hccv"))
        {
            io::StreamSource stream(*file);
            io::Reader reader(stream);
            if (!m_horizonColorCurve->Deserialize(reader))
            {
                ELOG("Failed to load horizon color curve");
                // Create default curve
                m_horizonColorCurve->Clear();
                m_horizonColorCurve->AddKey(0.0f, Vector4(0.02f, 0.05f, 0.1f, 1.0f));    // Night
                m_horizonColorCurve->AddKey(0.25f, Vector4(0.9f, 0.6f, 0.4f, 1.0f));     // Dawn
                m_horizonColorCurve->AddKey(0.5f, Vector4(0.5f, 0.7f, 1.0f, 1.0f));      // Midday
                m_horizonColorCurve->AddKey(0.75f, Vector4(0.9f, 0.6f, 0.4f, 1.0f));     // Dusk
                m_horizonColorCurve->AddKey(1.0f, Vector4(0.02f, 0.05f, 0.1f, 1.0f));    // Night
                m_horizonColorCurve->CalculateTangents();
            }
        }

        m_zenithColorCurve = std::make_unique<ColorCurve>();
        if (const auto file = AssetRegistry::OpenFile("Models/ZenithColor.hccv"))
        {
            io::StreamSource stream(*file);
            io::Reader reader(stream);
            if (!m_zenithColorCurve->Deserialize(reader))
            {
                ELOG("Failed to load zenith color curve");
                // Create default curve
                m_zenithColorCurve->Clear();
                m_zenithColorCurve->AddKey(0.0f, Vector4(0.01f, 0.03f, 0.08f, 1.0f));    // Night
                m_zenithColorCurve->AddKey(0.25f, Vector4(0.3f, 0.4f, 0.8f, 1.0f));      // Dawn
                m_zenithColorCurve->AddKey(0.5f, Vector4(0.1f, 0.3f, 0.9f, 1.0f));       // Midday
                m_zenithColorCurve->AddKey(0.75f, Vector4(0.3f, 0.4f, 0.8f, 1.0f));      // Dusk
                m_zenithColorCurve->AddKey(1.0f, Vector4(0.01f, 0.03f, 0.08f, 1.0f));    // Night
                m_zenithColorCurve->CalculateTangents();
            }
        }

		m_ambientColorCurve = std::make_unique<ColorCurve>();
        if (const auto file = AssetRegistry::OpenFile("Models/AmbientColor.hccv"))
        {
            io::StreamSource stream(*file);
            io::Reader reader(stream);
            if (!m_ambientColorCurve->Deserialize(reader))
            {
                ELOG("Failed to load ambient color curve");
                // Create default curve
                m_ambientColorCurve->Clear();
                m_ambientColorCurve->AddKey(0.0f, Vector4(0.01f, 0.03f, 0.08f, 1.0f));    // Night
                m_ambientColorCurve->AddKey(1.0f, Vector4(0.01f, 0.03f, 0.08f, 1.0f));    // Night
                m_ambientColorCurve->CalculateTangents();
            }
        }

		m_cloudColorCurve = std::make_unique<ColorCurve>();
        if (const auto file = AssetRegistry::OpenFile("Models/CloudColor.hccv"))
        {
            io::StreamSource stream(*file);
            io::Reader reader(stream);
            if (!m_cloudColorCurve->Deserialize(reader))
            {
                ELOG("Failed to load cloud color curve");
                // Create default curve
                m_cloudColorCurve->Clear();
                m_cloudColorCurve->AddKey(0.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f));    // Night
                m_cloudColorCurve->AddKey(1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f));    // Night
                m_cloudColorCurve->CalculateTangents();
            }
        }
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
        
        // Update lighting based on time of day
        UpdateLighting(GetNormalizedTimeOfDay());
    }

    void SkyComponent::SetPosition(const Vector3& position)
    {
        if (m_cloudsNode)
        {
            m_cloudsNode->SetPosition(position);
        }
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
    }    void SkyComponent::UpdateLighting(float normalizedTime)
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

        // Light color & intensity
        Vector4 sunColor(1.0f, 0.95f, 0.9f, 1.0f);
        float sunIntensity = 1.0f;

        Vector4 moonColor(0.3f, 0.4f, 0.65f, 1.0f);
        float moonIntensity = 0.12f;

        Vector4 blendedColor = sunColor * blendSun + moonColor * blendMoon;
        float blendedIntensity = sunIntensity * blendSun + moonIntensity * blendMoon;

        // Apply to shared light
        m_sunLight->SetDirection(lightDir);
        m_sunLight->SetColor(blendedColor);
        m_sunLight->SetIntensity(blendedIntensity);

        // Update light direction in material
        m_skyMatInst->SetVectorParameter("LightDirection", Vector4(lightDir.x, lightDir.y, lightDir.z, 0.0f));
        m_skyMatInst->SetScalarParameter("SunHeight", blendMoon);

        // Get colors from curves and apply to sky material
        const Vector4 horizonColor = m_horizonColorCurve->Evaluate(normalizedTime);
        const Vector4 zenithColor = m_zenithColorCurve->Evaluate(normalizedTime);
		const Vector4 ambientColor = m_ambientColorCurve->Evaluate(normalizedTime);
		const Vector4 cloudColor = m_cloudColorCurve->Evaluate(normalizedTime);
        m_skyMatInst->SetVectorParameter("HorizonColor", horizonColor);
        m_skyMatInst->SetVectorParameter("ZenithColor", zenithColor);
        m_skyMatInst->SetVectorParameter("CloudColor", cloudColor);
        
        // Update fog color based on horizon color
        m_scene.SetFogColor(Vector3(horizonColor.x, horizonColor.y, horizonColor.z));
        m_scene.SetAmbientColor(Vector3(ambientColor.x, ambientColor.y, ambientColor.z));
    }
}
