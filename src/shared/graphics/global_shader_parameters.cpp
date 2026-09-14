// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "global_shader_parameters.h"
#include "global_shader_parameters_serializer.h"
#include "graphics_device.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"

#include <cstring>

namespace mmo
{
	GlobalShaderParameters& GlobalShaderParameters::Get()
	{
		static GlobalShaderParameters instance;
		return instance;
	}

	void GlobalShaderParameters::Clear()
	{
		if (m_parameters.empty())
		{
			return;
		}

		m_parameters.clear();
		MarkLayoutDirty();
	}

	bool GlobalShaderParameters::DefineScalar(const std::string_view name, const float defaultValue)
	{
		if (name.empty() || Has(name))
		{
			return false;
		}

		GlobalShaderParameter param;
		param.name = String(name);
		param.type = global_shader_parameter_type::Scalar;
		param.value = Vector4(defaultValue, 0.0f, 0.0f, 0.0f);
		param.defaultValue = param.value;
		m_parameters.emplace_back(std::move(param));

		MarkLayoutDirty();
		return true;
	}

	bool GlobalShaderParameters::DefineVector(const std::string_view name, const Vector4& defaultValue)
	{
		if (name.empty() || Has(name))
		{
			return false;
		}

		GlobalShaderParameter param;
		param.name = String(name);
		param.type = global_shader_parameter_type::Vector;
		param.value = defaultValue;
		param.defaultValue = defaultValue;
		m_parameters.emplace_back(std::move(param));

		MarkLayoutDirty();
		return true;
	}

	bool GlobalShaderParameters::Remove(const std::string_view name)
	{
		const auto it = std::find_if(m_parameters.begin(), m_parameters.end(),
			[&name](const GlobalShaderParameter& p) { return p.name == name; });
		if (it == m_parameters.end())
		{
			return false;
		}

		m_parameters.erase(it);
		MarkLayoutDirty();
		return true;
	}

	bool GlobalShaderParameters::Rename(const std::string_view oldName, const std::string_view newName)
	{
		if (newName.empty() || Has(newName))
		{
			return false;
		}

		GlobalShaderParameter* param = FindMutable(oldName);
		if (!param)
		{
			return false;
		}

		param->name = String(newName);
		MarkLayoutDirty();
		return true;
	}

	bool GlobalShaderParameters::SetScalar(const std::string_view name, const float value)
	{
		GlobalShaderParameter* param = FindMutable(name);
		if (!param || param->type != global_shader_parameter_type::Scalar)
		{
			return false;
		}

		if (param->value.x != value)
		{
			param->value.x = value;
			m_dataDirty = true;
		}

		return true;
	}

	bool GlobalShaderParameters::SetVector(const std::string_view name, const Vector4& value)
	{
		GlobalShaderParameter* param = FindMutable(name);
		if (!param || param->type != global_shader_parameter_type::Vector)
		{
			return false;
		}

		if (param->value != value)
		{
			param->value = value;
			m_dataDirty = true;
		}

		return true;
	}

	bool GlobalShaderParameters::GetScalar(const std::string_view name, float& out_value) const
	{
		const GlobalShaderParameter* param = Find(name);
		if (!param || param->type != global_shader_parameter_type::Scalar)
		{
			return false;
		}

		out_value = param->value.x;
		return true;
	}

	bool GlobalShaderParameters::GetVector(const std::string_view name, Vector4& out_value) const
	{
		const GlobalShaderParameter* param = Find(name);
		if (!param || param->type != global_shader_parameter_type::Vector)
		{
			return false;
		}

		out_value = param->value;
		return true;
	}

	bool GlobalShaderParameters::SetDefault(const std::string_view name, const Vector4& value)
	{
		GlobalShaderParameter* param = FindMutable(name);
		if (!param)
		{
			return false;
		}

		param->defaultValue = value;
		if (param->value != value)
		{
			param->value = value;
			m_dataDirty = true;
		}

		return true;
	}

	bool GlobalShaderParameters::Has(const std::string_view name) const
	{
		return Find(name) != nullptr;
	}

	const GlobalShaderParameter* GlobalShaderParameters::Find(const std::string_view name) const
	{
		const auto it = std::find_if(m_parameters.begin(), m_parameters.end(),
			[&name](const GlobalShaderParameter& p) { return p.name == name; });
		return it != m_parameters.end() ? &(*it) : nullptr;
	}

	GlobalShaderParameter* GlobalShaderParameters::FindMutable(const std::string_view name)
	{
		const auto it = std::find_if(m_parameters.begin(), m_parameters.end(),
			[&name](const GlobalShaderParameter& p) { return p.name == name; });
		return it != m_parameters.end() ? &(*it) : nullptr;
	}

	void GlobalShaderParameters::MarkLayoutDirty()
	{
		m_layoutDirty = true;
		m_dataDirty = true;
		OnLayoutChanged();
	}

	size_t GlobalShaderParameters::ComputeBufferSize() const
	{
		size_t vectors = 0;
		size_t scalars = 0;
		for (const auto& p : m_parameters)
		{
			if (p.type == global_shader_parameter_type::Vector)
			{
				++vectors;
			}
			else
			{
				++scalars;
			}
		}

		// Vectors occupy a full 16-byte register each; scalars then pack tightly. Matches HLSL.
		size_t bytes = vectors * 16 + scalars * 4;

		// Constant buffers must never be empty and must be a multiple of 16 bytes.
		if (bytes == 0)
		{
			bytes = 16;
		}
		bytes = (bytes + 15) & ~static_cast<size_t>(15);
		return bytes;
	}

	std::vector<uint8> GlobalShaderParameters::BuildBufferData() const
	{
		std::vector<uint8> data;
		data.reserve(ComputeBufferSize());

		const auto appendFloat = [&data](const float f)
		{
			const auto* bytes = reinterpret_cast<const uint8*>(&f);
			data.insert(data.end(), bytes, bytes + sizeof(float));
		};

		// Vectors first (each a 16-byte aligned float4).
		for (const auto& p : m_parameters)
		{
			if (p.type == global_shader_parameter_type::Vector)
			{
				appendFloat(p.value.x);
				appendFloat(p.value.y);
				appendFloat(p.value.z);
				appendFloat(p.value.w);
			}
		}

		// Then scalars (packed contiguously - matches HLSL float packing after the float4 block).
		for (const auto& p : m_parameters)
		{
			if (p.type == global_shader_parameter_type::Scalar)
			{
				appendFloat(p.value.x);
			}
		}

		// Pad up to the (never-empty) 16-byte aligned buffer size.
		data.resize(ComputeBufferSize(), 0);
		return data;
	}

	ConstantBufferPtr GlobalShaderParameters::GetBuffer(GraphicsDevice& device)
	{
		if (m_layoutDirty)
		{
			m_buffer.reset();

			std::vector<uint8> data = BuildBufferData();
			m_buffer = device.CreateConstantBuffer(data.size(), data.data());

			m_layoutDirty = false;
			m_dataDirty = false;
		}
		else if (m_dataDirty)
		{
			std::vector<uint8> data = BuildBufferData();
			m_buffer->Update(data.data());
			m_dataDirty = false;
		}

		return m_buffer;
	}

	void GlobalShaderParameters::EnsureEngineDefaults()
	{
		// DefineVector fails harmlessly when the name already exists, so anything the asset
		// declared keeps its authored default and only genuinely missing entries are added.

		// Published by SkyComponent every frame from the time-of-day colour curves.
		DefineVector("SkyHorizonColor", Vector4(0.62f, 0.76f, 0.94f, 1.0f));
		DefineVector("SkyZenithColor", Vector4(0.24f, 0.45f, 0.82f, 1.0f));

		// Published by SkyComponent every frame. xyz points TOWARD the sun, matching the
		// convention Scene's forward camera constant buffer uses, so materials must not negate
		// it again. SunColor carries the blended sun/moon colour in rgb and intensity in a.
		DefineVector("SunDirection", Vector4(0.0f, 1.0f, 0.0f, 0.0f));
		DefineVector("SunColor", Vector4(1.0f, 0.95f, 0.9f, 1.0f));

		// Published by the host's WindSimulation every frame: xyz is the unit direction the wind blows
		// toward (y = 0), w the current speed in m/s including gusts.
		DefineVector("WindDirection", Vector4(0.70710678f, 0.0f, 0.70710678f, 0.0f));
	}

	bool GlobalShaderParameters::LoadFromAsset(const std::string_view assetPath)
	{
		const auto file = AssetRegistry::OpenFile(String(assetPath));
		if (!file)
		{
			// Not an error - the registry simply starts out empty until one is authored. The
			// engine's own parameters still have to exist, or every material referencing one
			// silently samples zero.
			EnsureEngineDefaults();
			return false;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		GlobalShaderParametersDeserializer deserializer{ *this };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to read global shader parameters from " << assetPath);
			EnsureEngineDefaults();
			return false;
		}

		EnsureEngineDefaults();

		ILOG("Loaded " << m_parameters.size() << " global shader parameter(s) from " << assetPath);
		return true;
	}

	bool GlobalShaderParameters::SaveToAsset(const std::string_view assetPath) const
	{
		const auto file = AssetRegistry::CreateNewFile(String(assetPath));
		if (!file)
		{
			ELOG("Failed to open " << assetPath << " for writing global shader parameters!");
			return false;
		}

		io::StreamSink sink{ *file };
		io::Writer writer{ sink };

		GlobalShaderParametersSerializer serializer;
		serializer.Export(*this, writer);

		file->flush();
		return true;
	}
}
