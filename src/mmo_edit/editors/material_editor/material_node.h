// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <any>
#include <filesystem>
#include <imgui.h>
#include <optional>
#include <span>
#include <variant>

#include "base/typedefs.h"
#include "frame_ui/color.h"

#include "imgui_node_editor.h"
#include "link_query_result.h"
#include "node_type_info.h"
#include "base/signal.h"
#include "graphics/material_compiler.h"
#include "material_function.h"

namespace ed = ax::NodeEditor;

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	class IMaterialGraphLoadContext;
	class MaterialCompiler;

	/// @brief Enumerates possible pin types.
	enum class PinType : uint8
	{
	    /// @brief Pin accepts material values.
	    Material,
	};
	
	class GraphNode;

	class PinValue
	{
	public:
	    using ValueType = std::variant<bool, int32_t, float, std::string>;

	    PinValue() = default;
	    PinValue(const PinValue&) = default;
	    PinValue(PinValue&&) = default;
	    PinValue& operator=(const PinValue&) = default;
	    PinValue& operator=(PinValue&&) = default;

	    PinValue(bool value): m_value(value) {}
	    PinValue(int32_t value): m_value(value) {}
	    PinValue(float value): m_value(value) {}
	    PinValue(std::string&& value): m_value(std::move(value)) {}
	    PinValue(const std::string& value): m_value(value) {}
	    PinValue(const char* value): m_value(std::string(value)) {}

	    PinType GetType() const { return static_cast<PinType>(m_value.index()); }

	    template <typename T>
	    T& As()
	    {
	        return get<T>(m_value);
	    }

	    template <typename T>
	    const T& As() const
	    {
	        return get<T>(m_value);
	    }

	private:
	    ValueType m_value;
	};
	
	class Pin
	{
	protected:
	    uint32 m_id = 0;
		GraphNode* m_node = nullptr;
		PinType m_type = PinType::Material;
	    String m_name;
		mutable const Pin* m_link = nullptr;

	public:
	    Pin(GraphNode* node, PinType type, std::string_view name = "");
		virtual ~Pin();

	public:
		virtual io::Writer& Serialize(io::Writer& writer);

		virtual io::Reader& Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context);

	public:
		virtual bool SetValueType(const PinType type) { return m_type == type; }

		[[nodiscard]] virtual PinType GetValueType() const { return m_type; }

		virtual bool SetValue(PinValue value) { return false; }

		[[nodiscard]] virtual PinValue GetValue() const { return PinValue{}; }

		[[nodiscard]] PinType GetType() const { return m_type; }

	    [[nodiscard]] LinkQueryResult CanLinkTo(const Pin& pin) const;

		bool LinkTo(const Pin& pin);

		void Unlink();

		[[nodiscard]] bool IsLinked() const { return m_link != nullptr; }

		[[nodiscard]] const Pin* GetLink() const { return m_link; }
		
		[[nodiscard]] bool IsInput() const;
		
		[[nodiscard]] bool IsOutput() const;
		
		[[nodiscard]] GraphNode* GetNode() const { return m_node; }

		[[nodiscard]] uint32 GetId() const { return m_id; }

	    [[nodiscard]] std::string_view GetName() const { return m_name; }

		void SetName(const String& name) { m_name = name; }
	};
	
	class MaterialPin final : public Pin
	{
	public:
		static constexpr auto TypeId = PinType::Material;

	    MaterialPin()
			: MaterialPin(nullptr)
	    {
	    }
	    MaterialPin(GraphNode* node)
			: Pin(node, PinType::Material)
	    {
	    }
	    MaterialPin(GraphNode* node, std::string_view name)
			: Pin(node, PinType::Material, name)
	    {
	    }

	    PinValue GetValue() const override { return const_cast<MaterialPin*>(this); }
	};
	
	namespace detail
	{
		constexpr inline uint32 fnv_1a_hash(const char string[], size_t size)
		{
		    constexpr uint32 c_offset_basis = 2166136261U;
		    constexpr uint32 c_prime        =   16777619U;

		    uint32 value = c_offset_basis;

		    auto   data = string;
		    while (size-- > 0)
		    {
		        value ^= static_cast<uint32>(static_cast<uint8>(*data++));
		        value *= c_prime;
		    }

		    return value;
		}

		template <size_t N>
		constexpr inline uint32 fnv_1a_hash(const char (&string)[N])
		{
		    return fnv_1a_hash(string, N - 1);
		}
	}

	# define MAT_NODE(type, displayName) \
    static NodeTypeInfo GetStaticTypeInfo() \
    { \
        return \
        { \
            detail::fnv_1a_hash(#type), \
            #type, \
            displayName, \
            [](MaterialGraph& materialGraph) -> GraphNode* { return new type(materialGraph); } \
        }; \
    } \
    \
    NodeTypeInfo GetTypeInfo() const override \
    { \
        return GetStaticTypeInfo(); \
    }

	class AssetPathValue
	{
	public:
		AssetPathValue(const std::string_view path, const std::string_view filter)
			: m_path(path)
			, m_filter(filter)
		{
		}

		void SetPath(const std::string_view value) noexcept { m_path = value; }

		std::string_view GetPath() const noexcept { return m_path; }

		std::string_view GetFilter() const noexcept { return m_filter; }

	private:
		String m_path;
		String m_filter;
	};

	/// @brief Non-generic base class of a node property.
	class PropertyBase
	{
	public:
		signal<void()> OnValueChanged;

	public:
		typedef std::variant<int32, float, String, bool, AssetPathValue, Color> ValueType;

	public:
		PropertyBase(const std::string_view name, const ValueType& value)
			: m_name(name)
			, m_value(value)
		{
		}
		virtual ~PropertyBase() = default;

	public:
		virtual io::Writer& Serialize(io::Writer& writer) = 0;

		virtual io::Reader& Deserialize(io::Reader& reader) = 0;

	public:
		/// @brief Gets the name of this property.
		[[nodiscard]] std::string_view GetName() const noexcept { return m_name; }

		/// @brief Gets a constant pointer to the value of the property in the specified type, or nullptr if the type isn't compatible.
		/// @tparam T The type to cast the value to.
		/// @return nullptr if the type is incompatible with the actual value, otherwise a const pointer to the value.
		template<typename T>
		const T* GetValueAs() const noexcept { return std::get_if<T>(&m_value); }

		/// @brief Sets the value of this property.
		/// @param value The new value to use.
		virtual void SetValue(const ValueType& value) noexcept { m_value = value; OnValueChanged(); }

	protected:
		std::string m_name;
		ValueType m_value;
	};

	/// @brief Generic base class of a node property with a specific type.
	/// @tparam TPropertyType Specific value type of the property.
	template<typename TPropertyType>
	class Property : public PropertyBase
	{
	public:
		Property(std::string_view name, TPropertyType& ref)
			: PropertyBase(name, ref)
			, m_ref(ref)
		{
		}
		
		virtual ~Property() = default;

	public:
		void SetValue(const ValueType& value) noexcept
		{
			const TPropertyType* newValue = std::get_if<TPropertyType>(&value);
			if (!newValue)
			{
				// Incompatible value
				return;
			}

			m_ref = *newValue;
			PropertyBase::SetValue(value);
		}

	protected:
		TPropertyType& m_ref;
	};
	
	/// @brief Bool implementation of a node property.
	class BoolProperty final : public Property<bool>
	{
	public:
		BoolProperty(std::string_view name, bool& ref)
			: Property(name, ref)
		{
		}

	public:
		io::Writer& Serialize(io::Writer& writer) override;
		io::Reader& Deserialize(io::Reader& reader) override;
	};

	/// @brief Float implementation of a node property.
	class FloatProperty final : public Property<float>
	{
	public:
		FloatProperty(std::string_view name, float& ref)
			: Property(name, ref)
		{
		}
		
	public:
		io::Writer& Serialize(io::Writer& writer) override;
		io::Reader& Deserialize(io::Reader& reader) override;
	};
	
	/// @brief Color implementation of a node property.
	class ColorProperty final : public Property<Color>
	{
	public:
		ColorProperty(std::string_view name, Color& ref)
			: Property(name, ref)
		{
		}
		
	public:
		io::Writer& Serialize(io::Writer& writer) override;
		io::Reader& Deserialize(io::Reader& reader) override;
	};

	/// @brief Int implementation of a node property.
	class IntProperty final : public Property<int>
	{
	public:
		IntProperty(std::string_view name, int32& ref)
			: Property(name, ref)
		{
		}
		
	public:
		io::Writer& Serialize(io::Writer& writer) override;
		io::Reader& Deserialize(io::Reader& reader) override;
	};

	/// @brief String implementation of a node property.
	class StringProperty final : public Property<String>
	{
	public:
		StringProperty(std::string_view name, String& ref)
			: Property(name, ref)
		{
		}

	public:
		io::Writer& Serialize(io::Writer& writer) override;
		io::Reader& Deserialize(io::Reader& reader) override;
	};

	/// @brief AssetPathValue implementation of a node property.
	class AssetPathProperty final : public Property<AssetPathValue>
	{
	public:
		AssetPathProperty(std::string_view name, AssetPathValue& ref)
			: Property(name, ref)
		{
		}
		
	public:
		io::Writer& Serialize(io::Writer& writer) override;
		io::Reader& Deserialize(io::Reader& reader) override;
	};

	/// @brief Enum implementation of a node property.
	template<typename TEnum>
	class EnumProperty final : public Property<int32>
	{
	public:
		using EnumType = TEnum;
		
		/// Represents an enum option in the property UI
		struct EnumOption
		{
			EnumType value;
			String displayName;
		};

		/// @brief Constructor with enum reference and list of options
		/// @param name The name of the property
		/// @param ref Reference to the enum value
		/// @param options Available options for this enum
		EnumProperty(std::string_view name, TEnum& ref, std::initializer_list<EnumOption> options)
			: Property(name, reinterpret_cast<int32&>(ref))
			, m_enumRef(ref)
			, m_options(options)
		{
		}
		
	public:
		/// @brief Sets the value of this property using the enum value
		/// @param value The new enum value
		void SetEnumValue(TEnum value) noexcept
		{
			m_enumRef = value;
			PropertyBase::SetValue(static_cast<int32>(value));
		}
		
		/// @brief Gets the current enum value
		/// @return The current enum value
		TEnum GetEnumValue() const noexcept
		{
			return m_enumRef;
		}
		
		/// @brief Gets all available options for this enum
		/// @return The list of enum options
		const std::vector<EnumOption>& GetOptions() const noexcept
		{
			return m_options;
		}
		
		/// @brief Find the display name for a specific enum value
		/// @param value The enum value to look up
		/// @return The display name for the enum value, or empty string if not found
		String GetDisplayName(TEnum value) const noexcept
		{
			for (const auto& option : m_options)
			{
				if (option.value == value)
				{
					return option.displayName;
				}
			}
			return "";
		}
		
		/// @brief Get the display name for the current enum value
		/// @return The display name of the current value
		String GetCurrentDisplayName() const noexcept
		{
			return GetDisplayName(m_enumRef);
		}

		io::Writer& Serialize(io::Writer& writer) override
		{
			writer << static_cast<int32>(m_enumRef);
			return writer;
		}

		io::Reader& Deserialize(io::Reader& reader) override
		{
			int32 value;
			reader >> value;
			m_enumRef = static_cast<TEnum>(value);
			return reader;
		}

	private:
		TEnum& m_enumRef;
		std::vector<EnumOption> m_options;
	};

	/// @brief Base class of a node in a MaterialGraph.
	class GraphNode
	{
		friend class MaterialGraph;

	public:
		/// @brief Creates a new instance of the Node class and initializes it.
		/// @param graph The material graph that this node belongs to.
		GraphNode(MaterialGraph& graph);

		/// @brief Virtual default destructor because of inheritance.
		virtual ~GraphNode() = default;

	public:
		/// @brief Serializes the contents of this node using a binary writer.
		/// @param writer The writer to write the state of this node to.
		/// @return The writer in the updated state.
		virtual io::Writer& Serialize(io::Writer& writer);

		/// @brief Deserializes the contents of this node using a binary reader.
		/// @param reader The reader to read the state of this node from.
		/// @param context A load context which allows to queue post-load actions.
		/// @return The reader in the updated state.
		virtual io::Reader& Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context);

	public:
	    template <typename T>
	    std::unique_ptr<T> CreatePin(const std::string_view name = "")
	    {
		    if (auto pin = CreatePin(T::TypeId, name))
		    {
			    return unique_ptr<T>(static_cast<T*>(pin.release()));
		    }
			
			return nullptr;
	    }

		virtual uint32 GetColor() { return IM_COL32(255, 255, 255, 64); }

	    std::unique_ptr<Pin> CreatePin(PinType pinType, std::string_view name = "");
		
		[[nodiscard]] virtual NodeTypeInfo GetTypeInfo() const { return NodeTypeInfo{}; }

	    [[nodiscard]] virtual std::string_view GetName() const;

	    [[nodiscard]] virtual LinkQueryResult AcceptLink(const Pin& receiver, const Pin& provider) const;

	    virtual void WasLinked(const Pin& receiver, const Pin& provider) { }

	    virtual void WasUnlinked(const Pin& receiver, const Pin& provider) { }

	    [[nodiscard]] virtual std::span<Pin*> GetInputPins() { return {}; }

	    [[nodiscard]] virtual std::span<Pin*> GetOutputPins() { return {}; }

		[[nodiscard]] MaterialGraph* GetMaterial() const { return m_material; }

		[[nodiscard]] uint32 GetId() const { return m_id; }

		[[nodiscard]] std::optional<uint32> GetPinIndex(const Pin& pin);

		virtual ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) = 0;

		[[nodiscard]] virtual std::span<PropertyBase*> GetProperties() { return {}; }

		virtual void NotifyCompilationStarted() { m_compiledExpressionId = IndexNone; }

	protected:
		uint32 m_id;
		MaterialGraph* m_material;
		ExpressionIndex m_compiledExpressionId { IndexNone };
	};

	/// @brief The main node of a material graph, which represents the output node of a material.
	class MaterialNode final : public GraphNode
	{
		static const uint32 Color;

	public:
	    MAT_NODE(MaterialNode, "Material")

	    MaterialNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
		std::span<Pin*> GetInputPins() override;
		
		[[nodiscard]] uint32 GetColor() override { return Color; }
		
		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override;

		const MaterialPin& GetBaseColorPin() const noexcept { return m_baseColor; }
		const MaterialPin& GetMetallicPin() const noexcept { return m_metallic; }
		const MaterialPin& GetSpecularPin() const noexcept { return m_specular; }
		const MaterialPin& GetRoughnessPin() const noexcept { return m_roughness; }
		const MaterialPin& GetEmissivePin() const noexcept { return m_emissive; }
		const MaterialPin& GetOpacityPin() const noexcept { return m_opacity; }
		const MaterialPin& GetOpacityMaskPin() const noexcept { return m_opacityMask; }
		const MaterialPin& GetNormalPin() const noexcept { return m_normal; }

	private:
		bool m_isTwoSided { false };
		bool m_receivesShadows { true };
		bool m_castShadows { true };
		bool m_depthTest { true };
		bool m_depthWrite { true };
		bool m_lit { true };
		bool m_translucent { false };
		bool m_userInterface{ false };
		
		BoolProperty m_litProperty { "Lit", m_lit };
		BoolProperty m_isTwoSidedProp { "Is Two Sided", m_isTwoSided };
		BoolProperty m_receivesShadowProp { "Receives Shadows", m_receivesShadows };
		BoolProperty m_castShadowProp { "Casts Shadows", m_receivesShadows };
		BoolProperty m_depthTestProp { "Depth Test", m_depthTest };
		BoolProperty m_depthWriteProp { "Depth Write", m_depthWrite };
		BoolProperty m_translucentProperty { "Translucent", m_translucent };
		BoolProperty m_userInterfaceProp { "User Interface", m_userInterface };

		PropertyBase* m_surfaceProperties[8] = { &m_litProperty, &m_isTwoSidedProp, &m_receivesShadowProp, &m_castShadowProp,
			&m_depthTestProp, &m_depthWriteProp, &m_translucentProperty, &m_userInterfaceProp };
		PropertyBase* m_userInterfaceProperties[1] = { &m_userInterfaceProp };

	    MaterialPin m_baseColor = { this, "Base Color" };
	    MaterialPin m_metallic = { this, "Metallic" };
		MaterialPin m_specular = { this, "Specular" };
		MaterialPin m_roughness = { this, "Roughness" };
		MaterialPin m_emissive = { this, "Emissive Color" };
		MaterialPin m_opacity = { this, "Opacity" };
		MaterialPin m_opacityMask = { this, "Opacity Mask" };
		MaterialPin m_normal = { this, "Normal" };

	    Pin* m_surfaceInputPins[8] = { &m_baseColor, &m_metallic, &m_specular, &m_roughness, &m_emissive, &m_opacity, &m_opacityMask, &m_normal };
	};

	/// @brief A node which adds a material function input expression.
	class MaterialFunctionInputNode final : public GraphNode
	{
		static const uint32 Color;

	public:
		MAT_NODE(MaterialFunctionInputNode, "Function Input")

		MaterialFunctionInputNode(MaterialGraph& material)
			: GraphNode(material)
			, m_name("Input")
			, m_description("")
			, m_sortPriority(0)
			, m_paramType(MaterialFunctionParamType::Float3)
			, m_defaultValue(0.0f)
		{
			m_nameChanged = m_nameProp.OnValueChanged.connect([this] { UpdatePinNames(); });
			m_paramTypeChanged = m_paramTypeProp.OnValueChanged.connect([this] { UpdatePinNames(); });
		}

		~MaterialFunctionInputNode() override
		{
			m_nameChanged.disconnect();
			m_paramTypeChanged.disconnect();
		}

		std::span<Pin*> GetInputPins() override { return {}; }
		std::span<Pin*> GetOutputPins() override { return m_outputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		std::string_view GetName() const override { return m_name; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

		void UpdatePinNames()
		{
			m_output.SetName(m_name);
		}

		/// @brief Gets the parameter name for this input
		[[nodiscard]] std::string_view GetParameterName() const { return m_name; }
		
		/// @brief Gets the parameter description
		[[nodiscard]] std::string_view GetParameterDescription() const { return m_description; }
		
		/// @brief Gets the sort priority for this parameter
		[[nodiscard]] int32 GetSortPriority() const { return m_sortPriority; }
		
		/// @brief Gets the parameter type
		[[nodiscard]] MaterialFunctionParamType GetParameterType() const { return m_paramType; }
		
		/// @brief Gets the default value for this parameter
		[[nodiscard]] float GetDefaultValue() const { return m_defaultValue; }

		io::Reader& Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context) override;

	private:
		String m_name;
		String m_description;
		int32 m_sortPriority;
		MaterialFunctionParamType m_paramType;
		float m_defaultValue;
		
		StringProperty m_nameProp{ "Name", m_name };
		StringProperty m_descriptionProp{ "Description", m_description };
		IntProperty m_sortPriorityProp{ "Sort Priority", m_sortPriority };
		EnumProperty<MaterialFunctionParamType> m_paramTypeProp{ "Parameter Type", m_paramType, {
			{ MaterialFunctionParamType::Float, "Float (1D)" },
			{ MaterialFunctionParamType::Float2, "Float2 (2D)" },
			{ MaterialFunctionParamType::Float3, "Float3 (3D/Color)" },
			{ MaterialFunctionParamType::Float4, "Float4 (4D)" },
			{ MaterialFunctionParamType::Texture, "Texture" }
		}};
		FloatProperty m_defaultValueProp{ "Default Value", m_defaultValue };
		
		scoped_connection m_nameChanged;
		scoped_connection m_paramTypeChanged;

		MaterialPin m_output = { this, m_name };
		Pin* m_outputPins[1] = { &m_output };
		
		PropertyBase* m_properties[5] = { 
			&m_nameProp, 
			&m_descriptionProp, 
			&m_sortPriorityProp,
			&m_paramTypeProp,
			&m_defaultValueProp 
		};
	};

	/// @brief A node which adds a constant float expression.
	class ConstFloatNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
	    MAT_NODE(ConstFloatNode, "Const Float")

	    ConstFloatNode(MaterialGraph& material)
			: GraphNode(material)
	    {
			m_valueChangedConnection = m_valueProperty.OnValueChanged.connect([this] { m_valueString = std::to_string(m_value); });
			m_valueString = std::to_string(m_value);
	    }

		[[nodiscard]] std::string_view GetName() const override { return m_valueString; }

	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		float m_value { 0.0f };
		FloatProperty m_valueProperty { "Value", m_value };

		scoped_connection m_valueChangedConnection;
		String m_valueString;

		PropertyBase* m_properties[1] = { &m_valueProperty };

	    MaterialPin m_Float = { this };

	    Pin* m_OutputPins[1] = { &m_Float };
	};

	/// @brief A node which adds a sine expression.
	class SineNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(SineNode, "Sine")

		SineNode(MaterialGraph& material)
			: GraphNode(material)
		{
		}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }

		std::span<Pin*> GetOutputPins() override { return m_OutputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	private:
		MaterialPin m_inputPin = { this };
		MaterialPin m_outputPin = { this };

		Pin* m_inputPins[1] = { &m_inputPin };
		Pin* m_OutputPins[1] = { &m_outputPin };
	};

	/// @brief A node which adds a cosine expression.
	class CosineNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(CosineNode, "Cosine")

		CosineNode(MaterialGraph& material)
			: GraphNode(material)
		{
		}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }

		std::span<Pin*> GetOutputPins() override { return m_OutputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	private:
		MaterialPin m_inputPin = { this };
		MaterialPin m_outputPin = { this };

		Pin* m_inputPins[1] = { &m_inputPin };
		Pin* m_OutputPins[1] = { &m_outputPin };
	};

	/// @brief A node which adds a tangent expression.
	class TangentNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(TangentNode, "Tangent")

		TangentNode(MaterialGraph& material)
			: GraphNode(material)
		{
		}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }

		std::span<Pin*> GetOutputPins() override { return m_OutputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	private:
		MaterialPin m_inputPin = { this };
		MaterialPin m_outputPin = { this };

		Pin* m_inputPins[1] = { &m_inputPin };
		Pin* m_OutputPins[1] = { &m_outputPin };
	};

	/// @brief A node which adds a atan2 expression.
	class ArcTangent2Node final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(ArcTangent2Node, "ArcTangent2")

		ArcTangent2Node(MaterialGraph& material)
			: GraphNode(material)
		{
		}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }

		std::span<Pin*> GetOutputPins() override { return m_OutputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	private:
		MaterialPin m_xPin = { this, "x" };
		MaterialPin m_yPin = { this, "y" };
		MaterialPin m_outputPin = { this };

		Pin* m_inputPins[2] = { &m_yPin, &m_xPin };
		Pin* m_OutputPins[1] = { &m_outputPin };
	};


	/// @brief A node which adds a frac expression.
	class FracNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(FracNode, "Frac")

		FracNode(MaterialGraph& material)
			: GraphNode(material)
		{
		}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }

		std::span<Pin*> GetOutputPins() override { return m_OutputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	private:
		MaterialPin m_inputPin = { this };
		MaterialPin m_outputPin = { this };

		Pin* m_inputPins[1] = { &m_inputPin };
		Pin* m_OutputPins[1] = { &m_outputPin };
	};

	/// @brief A node which adds a constant float expression.
	class ScalarParameterNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(ScalarParameterNode, "Scalar Parameter")

		ScalarParameterNode(MaterialGraph& material)
			: GraphNode(material)
		{}

		std::span<Pin*> GetOutputPins() override { return m_OutputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		[[nodiscard]] std::string_view GetName() const override { return m_name; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		String m_name;
		float m_value{ 0.0f };
		FloatProperty m_valueProperty{ "Value", m_value };
		StringProperty m_nameProperty{ "Name", m_name };

		PropertyBase* m_properties[2] = { &m_nameProperty, &m_valueProperty };

		MaterialPin m_Float = { this };

		Pin* m_OutputPins[1] = { &m_Float };
	};

	/// @brief 
	class IfNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(IfNode, "If")

		IfNode(MaterialGraph& material)
			: GraphNode(material)
		{}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }
		std::span<Pin*> GetOutputPins() override { return m_outputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		float m_threshold{ 0.00001f };
		FloatProperty m_thresholdProperty{ "Equals Threshold", m_threshold };

		PropertyBase* m_properties[1] = { &m_thresholdProperty };

		MaterialPin m_aPin = { this, "A" };
		MaterialPin m_bPin = { this, "B" };
		MaterialPin m_greaterPin = { this, "A > B" };
		MaterialPin m_equalsPin = { this, "A == B" };
		MaterialPin m_lessPin = { this, "A < B" };

		MaterialPin m_output = { this };

		Pin* m_inputPins[5] = {&m_aPin, &m_bPin, &m_greaterPin, &m_equalsPin, &m_lessPin};
		Pin* m_outputPins[1] = { &m_output };
	};

	/// @brief A node which adds a constant vector expression.
	class ConstVectorNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
	    MAT_NODE(ConstVectorNode, "Const Vector")

	    ConstVectorNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		mmo::Color m_value { Color::White };
		ColorProperty m_valueProperty { "Value", m_value };

		PropertyBase* m_properties[1] = { &m_valueProperty };

	    MaterialPin m_rgb = { this, "RGB" };
		MaterialPin m_r = { this, "R" };
		MaterialPin m_g = { this, "G" };
		MaterialPin m_b = { this, "B" };
		MaterialPin m_a = { this, "A" };
		MaterialPin m_argb = { this, "ARGB" };

	    Pin* m_OutputPins[6] = { &m_rgb, &m_r, &m_g, &m_b, &m_a, &m_argb };
	};

	/// @brief A node which adds a constant vector expression.
	class VectorParameterNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
		MAT_NODE(VectorParameterNode, "Vector Parameter")

		VectorParameterNode(MaterialGraph& material)
			: GraphNode(material)
		{}

		std::span<Pin*> GetOutputPins() override { return m_OutputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		[[nodiscard]] std::string_view GetName() const override { return m_name; }

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		String m_name;
		mmo::Color m_value{ Color::White };
		ColorProperty m_valueProperty{ "Value", m_value };
		StringProperty m_nameProperty{ "Name", m_name };

		PropertyBase* m_properties[2] = { &m_nameProperty, &m_valueProperty };

		MaterialPin m_rgb = { this, "RGB" };
		MaterialPin m_r = { this, "R" };
		MaterialPin m_g = { this, "G" };
		MaterialPin m_b = { this, "B" };
		MaterialPin m_a = { this, "A" };
		MaterialPin m_argb = { this, "ARGB" };

		Pin* m_OutputPins[6] = { &m_rgb, &m_r, &m_g, &m_b, &m_a, &m_argb };
	};

	/// @brief A node which adds an expression addition expression.
	class AddNode final : public GraphNode
	{
	public:
	    MAT_NODE(AddNode, "Add")

	    AddNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		float m_values[2] = { 1.0f, 1.0f };
		FloatProperty m_valueProperties[2] = { FloatProperty("Value 1", m_values[0]), FloatProperty("Value 2", m_values[1]) };

	    MaterialPin m_input1 = { this, "A" };
		MaterialPin m_input2 = { this, "B" };
		
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[2] = { &m_valueProperties[0], &m_valueProperties[1] };
	    Pin* m_inputPins[2] = { &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	/// @brief A node which adds an expression multiplication expression.
	class MultiplyNode final : public GraphNode
	{
	public:
	    MAT_NODE(MultiplyNode, "Multiply")

	    MultiplyNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		float m_values[2] = { 1.0f, 1.0f };
		FloatProperty m_valueProperties[2] = { FloatProperty("Value 1", m_values[0]), FloatProperty("Value 2", m_values[1]) };

	    MaterialPin m_input1 = { this, "A" };
		MaterialPin m_input2 = { this, "B" };
		
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[2] = { &m_valueProperties[0], &m_valueProperties[1] };
	    Pin* m_inputPins[2] = { &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which applies a mask to the RGBA output of an expression and builds a new expression from it.
	class MaskNode final : public GraphNode
	{
	public:
	    MAT_NODE(MaskNode, "Mask")

	    MaskNode(MaterialGraph& material)
			: GraphNode(material)
	    {
			m_maskedChanged += {
				m_valueProperties[0].OnValueChanged.connect([this] { OnMaskChanged(); }),
				m_valueProperties[1].OnValueChanged.connect([this] { OnMaskChanged(); }),
				m_valueProperties[2].OnValueChanged.connect([this] { OnMaskChanged(); }),
				m_valueProperties[3].OnValueChanged.connect([this] { OnMaskChanged(); })
			};

			OnMaskChanged();
	    }
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		[[nodiscard]] std::string_view GetName() const override { return m_name; }

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		void OnMaskChanged()
		{
			std::ostringstream strm;
			strm << "Mask ( ";
			if (m_channels[0]) strm << "R ";
			if (m_channels[1]) strm << "G ";
			if (m_channels[2]) strm << "B ";
			if (m_channels[3]) strm << "A ";
			strm << ")";
			m_name = strm.str();
		}

	private:
		String m_name;
		scoped_connection_container m_maskedChanged;

		bool m_channels[4] = { true, true, false, false };
		BoolProperty m_valueProperties[4] = {
			BoolProperty("R", m_channels[0]),
			BoolProperty("G", m_channels[1]),
			BoolProperty("B", m_channels[2]),
			BoolProperty("A", m_channels[3])
		};

	    MaterialPin m_input = { this };
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[4] = { &m_valueProperties[0], &m_valueProperties[1], &m_valueProperties[2], &m_valueProperties[3] };
	    Pin* m_inputPins[1] = { &m_input };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	class DotNode final : public GraphNode
	{
	public:
	    MAT_NODE(DotNode, "Dot")

	    DotNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    MaterialPin m_input1 = { this, "A" };
		MaterialPin m_input2 = { this, "B" };
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[2] = { &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	class OneMinusNode final : public GraphNode
	{
	public:
	    MAT_NODE(OneMinusNode, "One Minus")

	    OneMinusNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    MaterialPin m_input = { this };
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[1] = { &m_input };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	class ClampNode final : public GraphNode
	{
	public:
	    MAT_NODE(ClampNode, "Clamp")

	    ClampNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		float m_values[2] = { 0.0f, 1.0f };
		FloatProperty m_valueProperties[2] = { FloatProperty("Min Default", m_values[0]), FloatProperty("Max Default", m_values[1]) };
		
	    MaterialPin m_input = { this };
	    MaterialPin m_input1 = { this, "Min" };
		MaterialPin m_input2 = { this, "Max" };
		
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[2] = { &m_valueProperties[0], &m_valueProperties[1] };
	    Pin* m_inputPins[3] = { &m_input, &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	class PowerNode final : public GraphNode
	{
	public:
	    MAT_NODE(PowerNode, "Power")

	    PowerNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		float m_exponent { 2.0f };
		FloatProperty m_exponentProp = FloatProperty{ "Const Exponent", m_exponent };

	    MaterialPin m_input1 = { this, "Base" };
		MaterialPin m_input2 = { this, "Exp" };
		
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[1] = { &m_exponentProp };
	    Pin* m_inputPins[2] = { &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which adds a linear interpolation expression.
	class LerpNode final : public GraphNode
	{
	public:
	    MAT_NODE(LerpNode, "Lerp")

	    LerpNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		float m_values[3] = { 0.0f, 1.0f, 0.0f };
		FloatProperty m_valueProperties[3] = { FloatProperty("Value A", m_values[0]), FloatProperty("Value B", m_values[1]), FloatProperty("Alpha", m_values[2]) };

	    MaterialPin m_input1 = { this, "A" };
		MaterialPin m_input2 = { this, "B" };
		MaterialPin m_input3 = { this, "Alpha" };
		
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[3] = { &m_valueProperties[0], &m_valueProperties[1], &m_valueProperties[2] };
	    Pin* m_inputPins[3] = { &m_input1, &m_input2, &m_input3 };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	/// @brief A node which provides a texture coordinate expression.
	class TextureCoordNode final : public GraphNode
	{
	public:
		static const uint32 Color;

	public:
	    MAT_NODE(TextureCoordNode, "TexCoord")

	    TextureCoordNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		/// @brief Index of the uv coordinate set to use.
		int32 m_uvCoordIndex { 0 };

		IntProperty m_uvCoordIndexProp { "UV Coordinate Index", m_uvCoordIndex };

		PropertyBase* m_properties[1] = { &m_uvCoordIndexProp };

	    /// @brief The uv output pin.
	    MaterialPin m_uvs = { this };

	    /// @brief List of output pins as an array.
	    Pin* m_outputPins[1] = { &m_uvs };
	};
	
	/// @brief A node which provides a pixel's world position as expression.
	class WorldPositionNode final : public GraphNode
	{
	public:
	    MAT_NODE(WorldPositionNode, "World Position")

	    WorldPositionNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return TextureCoordNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    /// @brief The uv output pin.
	    MaterialPin m_coordinates = { this, "\0" };

	    /// @brief List of output pins as an array.
	    Pin* m_outputPins[1] = { &m_coordinates };
	};
	
	/// @brief A node which provides a pixel's camera vector (view direction) as expression.
	class CameraVectorNode final : public GraphNode
	{
	public:
	    MAT_NODE(CameraVectorNode, "Camera Vector")

	    CameraVectorNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return TextureCoordNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    /// @brief The uv output pin.
	    MaterialPin m_output = { this };

	    /// @brief List of output pins as an array.
	    Pin* m_outputPins[1] = { &m_output };
	};
	
	/// @brief A node which provides a pixel's world position as expression.
	class VertexNormalNode final : public GraphNode
	{
	public:
	    MAT_NODE(VertexNormalNode, "Vertex Normal")

	    VertexNormalNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return TextureCoordNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    /// @brief The uv output pin.
	    MaterialPin m_coordinates = { this };

	    /// @brief List of output pins as an array.
	    Pin* m_outputPins[1] = { &m_coordinates };
	};
	
	/// @brief A node which provides a pixel's interpolated vertex color as expression.
	class VertexColorNode final : public GraphNode
	{
	public:
	    MAT_NODE(VertexColorNode, "Vertex Color")

	    VertexColorNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return TextureCoordNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    /// @brief The uv output pin.
	    MaterialPin m_coordinates = { this };

	    /// @brief List of output pins as an array.
	    Pin* m_outputPins[1] = { &m_coordinates };
	};
	
	class AbsNode final : public GraphNode
	{
	public:
	    MAT_NODE(AbsNode, "Abs")

	    AbsNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    MaterialPin m_input = { this };
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[1] = { &m_input };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which adds an expression multiplication expression.
	class DivideNode final : public GraphNode
	{
	public:
	    MAT_NODE(DivideNode, "Divide")

	    DivideNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		float m_values[2] = { 1.0f, 1.0f };
		FloatProperty m_valueProperties[2] = { FloatProperty("Value 1", m_values[0]), FloatProperty("Value 2", m_values[1]) };

	    MaterialPin m_input1 = { this, "A" };
		MaterialPin m_input2 = { this, "B" };
		
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[2] = { &m_valueProperties[0], &m_valueProperties[1] };
	    Pin* m_inputPins[2] = { &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which adds an expression subtraction expression.
	class SubtractNode final : public GraphNode
	{
	public:
	    MAT_NODE(SubtractNode, "Subtract")

	    SubtractNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
		float m_values[2] = { 1.0f, 1.0f };
		FloatProperty m_valueProperties[2] = { FloatProperty("Value 1", m_values[0]), FloatProperty("Value 2", m_values[1]) };

	    MaterialPin m_input1 = { this, "A" };
		MaterialPin m_input2 = { this, "B" };
		
	    MaterialPin m_output = { this };

		PropertyBase* m_properties[2] = { &m_valueProperties[0], &m_valueProperties[1] };
	    Pin* m_inputPins[2] = { &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which adds an expression subtraction expression.
	class WorldToTangentNormalNode final : public GraphNode
	{
	public:
	    MAT_NODE(WorldToTangentNormalNode, "World Space to Tangent Space Normal")

	    WorldToTangentNormalNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

	private:
	    MaterialPin m_input = { this };
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[1] = { &m_input };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which adds an expression normalization expression.
	class NormalizeNode final : public GraphNode
	{
	public:
	    MAT_NODE(NormalizeNode, "Normalize")

	    NormalizeNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    MaterialPin m_input = { this };
		
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[1] = { &m_input };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which appends an input expression's value components.
	class AppendNode final : public GraphNode
	{
	public:
	    MAT_NODE(AppendNode, "Append")

	    AppendNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;
		
	private:
	    MaterialPin m_inputA = { this, "A" };
		MaterialPin m_inputB = { this, "B" };
		
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[2] = { &m_inputA, &m_inputB };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	/// @brief A node which adds a texture sample expression.
	class TextureNode final : public GraphNode
	{
		static const uint32 Color;

	public:
	    MAT_NODE(TextureNode, "Texture")

	    TextureNode(MaterialGraph& material)
			: GraphNode(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		[[nodiscard]] std::string_view GetTexture() const { return m_texturePath.GetPath(); }
		
		void SetTexture(const std::string_view texture) { m_texturePath.SetPath(texture); }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		AssetPathValue m_texturePath { "", ".htex" };
		AssetPathProperty m_texturePathProp { "Texture", m_texturePath };

		int m_samplerType = 0;
		IntProperty m_samplerTypeProp{ "Sampler Type", m_samplerType };

		PropertyBase* m_properties[2] = { &m_texturePathProp, &m_samplerTypeProp };

	    MaterialPin m_uvs = { this, "UVs" };

	    MaterialPin m_rgb = { this, "RGB" };
		MaterialPin m_r = { this, "R" };
		MaterialPin m_g = { this, "G" };
		MaterialPin m_b = { this, "B" };
		MaterialPin m_a = { this, "A" };
		MaterialPin m_rgba = { this, "RGBA" };
		
	    Pin* m_inputPins[1] = { &m_uvs };
	    Pin* m_outputPins[6] = { &m_rgb, &m_r, &m_g, &m_b, &m_a, &m_rgba };
	};

	/// @brief A node which adds a texture sample expression which uses a named texture parameter.
	class TextureParameterNode final : public GraphNode
	{
		static const uint32 Color;

	public:
		MAT_NODE(TextureParameterNode, "Texture Parameter")

		TextureParameterNode(MaterialGraph& material)
			: GraphNode(material)
		{}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }
		std::span<Pin*> GetOutputPins() override { return m_outputPins; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		[[nodiscard]] std::string_view GetTexture() const { return m_texturePath.GetPath(); }

		void SetTexture(const std::string_view texture) { m_texturePath.SetPath(texture); }

		[[nodiscard]] std::string_view GetName() const override { return m_textureName.empty() ? GraphNode::GetName() : m_textureName; }

		void SetName(const std::string_view name) { m_textureName = name; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		String m_textureName;
		AssetPathValue m_texturePath{ "", ".htex" };
		AssetPathProperty m_texturePathProp{ "Texture", m_texturePath };
		StringProperty m_nameProp{ "Name", m_textureName };

		int m_samplerType = 0;
		IntProperty m_samplerTypeProp{ "Sampler Type", m_samplerType };

		PropertyBase* m_properties[3] = { &m_nameProp, &m_texturePathProp, &m_samplerTypeProp };

		MaterialPin m_uvs = { this, "UVs" };

		MaterialPin m_rgb = { this, "RGB" };
		MaterialPin m_r = { this, "R" };
		MaterialPin m_g = { this, "G" };
		MaterialPin m_b = { this, "B" };
		MaterialPin m_a = { this, "A" };
		MaterialPin m_rgba = { this, "RGBA" };

		Pin* m_inputPins[1] = { &m_uvs };
		Pin* m_outputPins[6] = { &m_rgb, &m_r, &m_g, &m_b, &m_a, &m_rgba };
	};

	/// @brief A node which adds a material function output expression.
	class MaterialFunctionOutputNode final : public GraphNode
	{
		static const uint32 Color;

	public:
		MAT_NODE(MaterialFunctionOutputNode, "Material Function Output")

		MaterialFunctionOutputNode(MaterialGraph& material)
		: GraphNode(material)
		, m_name("Result")
		{
			// Connect the property change event
			m_nameChanged = m_nameProp.OnValueChanged.connect([this] { 
				RefreshOutputPins();
			});
			
			// Initialize with a default output pin
			RefreshOutputPins();
		}

		~MaterialFunctionOutputNode() override
		{
			m_nameChanged.disconnect();
		}

		std::span<Pin*> GetInputPins() override { return m_inputPins; }

		std::span<Pin*> GetOutputPins() override { return {}; }

		[[nodiscard]] uint32 GetColor() override { return Color; }

		std::string_view GetName() const override { return m_name; }

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

		/// @brief Gets the parameter type
		[[nodiscard]] MaterialFunctionParamType GetParameterType() const { return m_paramType; }

		void RefreshOutputPins()
		{
			// Clear existing pins
			for (auto pin : m_inputPins)
			{
				if (pin && pin->IsLinked())
				{
					pin->Unlink();
				}
			}
			m_inputPins.clear();
			
			// Create a new input pin with the name from the property
			auto newPin = new MaterialPin(this, m_name);
			m_inputPins.push_back(newPin);
		}

		// Add a method to get all function output connections
		std::vector<std::pair<String, ExpressionIndex>> GetFunctionOutputs(MaterialCompiler& compiler)
		{
			std::vector<std::pair<String, ExpressionIndex>> outputs;
			
			// For each input pin, get its node and compile it
			for (auto pin : m_inputPins)
			{
				if (pin && pin->IsLinked())
				{
					auto linkedPin = pin->GetLink();
					auto linkedNode = linkedPin->GetNode();
					auto expressionIndex = linkedNode->Compile(compiler, linkedPin);
					outputs.emplace_back(pin->GetName(), expressionIndex);
				}
			}
			
			return outputs;
		}

	private:
		String m_name;
		StringProperty m_nameProp{ "Output Name", m_name };

		MaterialFunctionParamType m_paramType = MaterialFunctionParamType::Float3;
		EnumProperty<MaterialFunctionParamType> m_paramTypeProp{ "Parameter Type", m_paramType, {
			{ MaterialFunctionParamType::Float, "Float (1D)" },
			{ MaterialFunctionParamType::Float2, "Float2 (2D)" },
			{ MaterialFunctionParamType::Float3, "Float3 (3D/Color)" },
			{ MaterialFunctionParamType::Float4, "Float4 (4D)" },
			{ MaterialFunctionParamType::Texture, "Texture" }
		} };

		PropertyBase* m_properties[2] = { &m_nameProp, &m_paramTypeProp };

		std::vector<Pin*> m_inputPins;
		scoped_connection m_nameChanged;
	};

	/// @brief A node which adds a material function expression.
	class MaterialFunctionNode final : public GraphNode
	{
		static const uint32 Color;

	public:
		MAT_NODE(MaterialFunctionNode, "Material Function")

		MaterialFunctionNode(MaterialGraph& material)
		: GraphNode(material)
		, m_name("Material Function")
		{
			m_materialFunctionChanged = m_materialFunctionPathProp.OnValueChanged.connect([this] { 
				m_name = std::filesystem::path(m_materialFunctionPath.GetPath()).filename().replace_extension().string(); 
				// Clear cached expressions when the material function changes
				m_compiledExpressionCache.clear();
				// Refresh pins based on the material function's inputs/outputs
				RefreshPins();
			});
		}

		std::span<Pin*> GetInputPins() override;
		std::span<Pin*> GetOutputPins() override;

		[[nodiscard]] uint32 GetColor() override { return Color; }

		[[nodiscard]] std::string_view GetMaterialFunction() const { return m_materialFunctionPath.GetPath(); }

		void SetMaterialFunction(const std::string_view texture) { m_materialFunctionPath.SetPath(texture); }

		std::string_view GetName() const override;

		ExpressionIndex Compile(MaterialCompiler& compiler, const Pin* outputPin) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

		void RefreshPins();

		void NotifyCompilationStarted() override 
		{ 
			m_compiledExpressionCache.clear(); 
			GraphNode::NotifyCompilationStarted();
		}

		io::Writer& Serialize(io::Writer& writer) override;
		io::Reader& Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context) override;

	private:
		String m_name;
		AssetPathValue m_materialFunctionPath{ "", ".hmf" };
		AssetPathProperty m_materialFunctionPathProp{ "Material Function", m_materialFunctionPath };
		scoped_connection m_materialFunctionChanged;

		// Output pin to expression index mapping for caching
		struct CompiledExpression
		{
			const Pin* outputPin;
			ExpressionIndex expressionId;
		};
		std::vector<CompiledExpression> m_compiledExpressionCache;

		// Input and output pins that will be dynamically updated based on the material function
		std::vector<Pin*> m_inputPins;
		std::vector<Pin*> m_outputPins;

		PropertyBase* m_properties[1] = { &m_materialFunctionPathProp };
	};
}
