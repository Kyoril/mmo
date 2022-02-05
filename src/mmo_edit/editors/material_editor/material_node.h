#pragma once

#include <any>
#include <imgui.h>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "base/typedefs.h"
#include "frame_ui/color.h"

#include "imgui_node_editor.h"
#include "link_query_result.h"
#include "node_type_info.h"
#include "graphics/material_compiler.h"

namespace ed = ax::NodeEditor;

namespace mmo
{
	class MaterialCompiler;
	class MaterialGraph;

	/// @brief Enumerates possible pin types.
	enum class PinType : uint8
	{
		/// @brief Pin accepts any value type.
		Any,

	    /// @brief Pin accepts boolean values.
	    Bool,

	    /// @brief Pin accepts integer values.
	    Int32,

	    /// @brief Pin accepts floating point values.
	    Float,

	    /// @brief Pin accepts material values.
	    Material,

	    /// @brief Pin accepts string values.
	    String
	};
	
	class Node;

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
	    Node* m_node = nullptr;
		PinType m_type = PinType::Material;
	    std::string_view m_name;
		mutable const Pin* m_link = nullptr;

	public:
	    Pin(Node* node, PinType type, std::string_view name = "");
		virtual ~Pin();

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
		
		[[nodiscard]] Node* GetNode() const { return m_node; }

		[[nodiscard]] uint32 GetId() const { return m_id; }

	    [[nodiscard]] std::string_view GetName() const { return m_name; }
	};

	class AnyPin final : public Pin
	{
	public:
		static constexpr auto TypeId = PinType::Any;

		AnyPin(Node* node, const std::string_view name = "")
			: Pin(node, PinType::Any, name)
		{
		}

	public:
		bool SetValueType(PinType type) override;

		bool SetValue(PinValue value) override;

		[[nodiscard]] PinType GetValueType() const override { return m_innerPin ? m_innerPin->GetValueType() : Pin::GetValueType(); }

	private:
		std::unique_ptr<Pin> m_innerPin;
	};

	class BoolPin final : public Pin
	{
	public:
		static constexpr auto TypeId = PinType::Any;

		 BoolPin(Node* node, bool value = false)
	        : Pin(node, PinType::Bool)
	        , m_Value(value)
	    {
	    }

	    // C++ implicitly convert literals to bool, this will intercept
	    // such calls an do the right thing.
	    template <size_t N>
	    BoolPin(Node* node, const char (&name)[N], const bool value = false)
	        : Pin(node, PinType::Bool, name)
	        , m_Value(value)
	    {
	    }

	    BoolPin(Node* node, const std::string_view name, const bool value = false)
	        : Pin(node, PinType::Bool, name)
	        , m_Value(value)
	    {
	    }
		
	    bool SetValue(PinValue value) override
	    {
	        if (value.GetType() != TypeId)
	            return false;

	        m_Value = value.As<bool>();

	        return true;
	    }

	    PinValue GetValue() const override { return m_Value; }


	private:
		bool m_Value = false;
	};
	
	class Int32Pin final : public Pin
	{
	public:
	    static constexpr auto TypeId = PinType::Int32;

	    Int32Pin(Node* node, int32_t value = 0): Pin(node, PinType::Int32), m_value(value) {}
	    Int32Pin(Node* node, std::string_view name, int32_t value = 0): Pin(node, PinType::Int32, name), m_value(value) {}

	public:
	    bool SetValue(PinValue value) override
	    {
	        if (value.GetType() != TypeId)
	            return false;

	        m_value = value.As<int32_t>();

	        return true;
	    }

	    PinValue GetValue() const override { return m_value; }

	private:
	    int32_t m_value = 0;
	};

	class FloatPin final : public Pin
	{
	public:
	    static constexpr auto TypeId = PinType::Float;

	    FloatPin(Node* node, float value = 0.0f): Pin(node, PinType::Float), m_value(value) {}
	    FloatPin(Node* node, std::string_view name, float value = 0.0f): Pin(node, PinType::Float, name), m_value(value) {}

	public:
	    bool SetValue(PinValue value) override
	    {
	        if (value.GetType() != TypeId)
	            return false;

	        m_value = value.As<float>();

	        return true;
	    }

	    PinValue GetValue() const override { return m_value; }

		[[nodiscard]] std::string_view GetStringValue() const
	    {
		    if (m_stringDirty)
		    {
				m_stringValue = std::to_string(m_value);
			    m_stringDirty = false;
		    }

			return m_stringValue;
	    }

	private:
	    float m_value = 0.0f;
		mutable std::string m_stringValue;
		mutable bool m_stringDirty { true };
	};
	
	class StringPin final : public Pin
	{
	public:
	    static constexpr auto TypeId = PinType::String;

	    StringPin(Node* node, std::string value = "")
			: Pin(node, PinType::String)
			, m_value(value)
		{}

	    StringPin(Node* node, std::string_view name, std::string value = "")
			: Pin(node, PinType::String, name)
			, m_value(value)
		{}

	public:
	    bool SetValue(PinValue value) override
	    {
	        if (value.GetType() != TypeId)
	        {
				return false;   
	        }

	        m_value = value.As<std::string>();
	        return true;
	    }

	    PinValue GetValue() const override { return m_value; }

	private:
	    std::string m_value;
	};

	class MaterialPin final : public Pin
	{
	public:
		static constexpr auto TypeId = PinType::Material;

	    MaterialPin()
			: MaterialPin(nullptr)
	    {
	    }
	    MaterialPin(Node* node)
			: Pin(node, PinType::Material)
	    {
	    }
	    MaterialPin(Node* node, std::string_view name)
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
            [](MaterialGraph& materialGraph) -> Node* { return new type(materialGraph); } \
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
		typedef std::variant<int32, float, String, bool, AssetPathValue, Color> ValueType;

	public:
		PropertyBase(std::string_view name, const ValueType& value)
			: m_name(name)
			, m_value(value)
		{
		}
		virtual ~PropertyBase() = default;

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
		virtual void SetValue(const ValueType& value) noexcept { m_value = value; }

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

			PropertyBase::SetValue(value);
			m_ref = *newValue;
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
	};

	/// @brief Float implementation of a node property.
	class FloatProperty final : public Property<float>
	{
	public:
		FloatProperty(std::string_view name, float& ref)
			: Property(name, ref)
		{
		}
	};
	
	/// @brief Color implementation of a node property.
	class ColorProperty final : public Property<Color>
	{
	public:
		ColorProperty(std::string_view name, Color& ref)
			: Property(name, ref)
		{
		}
	};

	/// @brief Int implementation of a node property.
	class IntProperty final : public Property<int>
	{
	public:
		IntProperty(std::string_view name, int32& ref)
			: Property(name, ref)
		{
		}
	};

	/// @brief String implementation of a node property.
	class StringProperty final : public Property<String>
	{
	public:
		StringProperty(std::string_view name, String& ref)
			: Property(name, ref)
		{
		}
	};

	/// @brief AssetPathValue implementation of a node property.
	class AssetPathProperty final : public Property<AssetPathValue>
	{
	public:
		AssetPathProperty(std::string_view name, AssetPathValue& ref)
			: Property(name, ref)
		{
		}
	};

	/// @brief Base class of a node in a MaterialGraph.
	class Node
	{
		friend class MaterialGraph;

	public:
		Node(MaterialGraph& material);
		virtual ~Node() = default;

	public:
	    template <typename T>
	    std::unique_ptr<T> CreatePin(std::string_view name = "")
	    {
		    if (auto pin = CreatePin(T::TypeId, name))
		    {
			    return unique_ptr<T>(static_cast<T*>(pin.release()));
		    }
			
			return nullptr;
	    }

		virtual uint32 GetColor() { return IM_COL32(255, 255, 255, 64); }

	    std::unique_ptr<Pin> CreatePin(PinType pinType, std::string_view name = "");
		
		virtual NodeTypeInfo GetTypeInfo() const { return {}; }

	    virtual std::string_view GetName() const;

	    virtual LinkQueryResult AcceptLink(const Pin& receiver, const Pin& provider) const;

	    virtual void WasLinked(const Pin& receiver, const Pin& provider) { }

	    virtual void WasUnlinked(const Pin& receiver, const Pin& provider) { }

	    virtual std::span<Pin*> GetInputPins() { return {}; }

	    virtual std::span<Pin*> GetOutputPins() { return {}; }

		MaterialGraph* GetMaterial() const { return m_material; }

		uint32 GetId() const { return m_id; }

		std::optional<uint32> GetPinIndex(const Pin& pin);

		virtual ExpressionIndex Compile(MaterialCompiler& compiler) = 0;

		virtual std::span<PropertyBase*> GetProperties() { return {}; }

		virtual void BeginCompile() { m_compiledExpressionId = IndexNone; }

	protected:
		uint32 m_id;
		MaterialGraph* m_material;
		ExpressionIndex m_compiledExpressionId { IndexNone };
	};

	/// @brief The main node of a material graph, which represents the output node of a material.
	class MaterialNode final : public Node
	{
		static const uint32 Color;

	public:
	    MAT_NODE(MaterialNode, "Material")

	    MaterialNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_InputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }
		
		ExpressionIndex Compile(MaterialCompiler& compiler) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

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
		bool m_castsShadows { true };
		bool m_depthTest { true };
		bool m_depthWrite { true };
		bool m_lit { true };
		
		BoolProperty m_litProperty { "Lit", m_lit };
		BoolProperty m_isTwoSidedProp { "Is Two Sided", m_isTwoSided };
		BoolProperty m_receivesShadowProp { "Receives Shadows", m_receivesShadows };
		BoolProperty m_castShadowProp { "Casts Shadows", m_receivesShadows };
		BoolProperty m_depthTestProp { "Depth Test", m_depthTest };
		BoolProperty m_depthWriteProp { "Depth Write", m_depthWrite };

		PropertyBase* m_properties[6] = { &m_litProperty, &m_isTwoSidedProp, &m_receivesShadowProp, &m_castShadowProp,
			&m_depthTestProp, &m_depthWriteProp };

	    MaterialPin m_baseColor = { this, "Base Color" };
	    MaterialPin m_metallic = { this, "Metallic" };
		MaterialPin m_specular = { this, "Specular" };
		MaterialPin m_roughness = { this, "Roughness" };
		MaterialPin m_emissive = { this, "Emissive Color" };
		MaterialPin m_opacity = { this, "Opacity" };
		MaterialPin m_opacityMask = { this, "Opacity Mask" };
		MaterialPin m_normal = { this, "Normal" };

	    Pin* m_InputPins[8] = { &m_baseColor, &m_metallic, &m_specular, &m_roughness, &m_emissive, &m_opacity, &m_opacityMask, &m_normal };
	};

	/// @brief A node which adds a constant float expression.
	class ConstFloatNode final : public Node
	{
	public:
		static const uint32 Color;

	public:
	    MAT_NODE(ConstFloatNode, "Const Float")

	    ConstFloatNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		float m_value { 0.0f };
		FloatProperty m_valueProperty { "Value", m_value };

		PropertyBase* m_properties[1] = { &m_valueProperty };

	    MaterialPin m_Float = { this };

	    Pin* m_OutputPins[1] = { &m_Float };
	};
	
	/// @brief A node which adds a constant vector expression.
	class ConstVectorNode final : public Node
	{
	public:
		static const uint32 Color;

	public:
	    MAT_NODE(ConstVectorNode, "Const Vector")

	    ConstVectorNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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
	
	/// @brief A node which adds an expression addition expression.
	class AddNode final : public Node
	{
	public:
	    MAT_NODE(AddNode, "Add")

	    AddNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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
	class MultiplyNode final : public Node
	{
	public:
	    MAT_NODE(MultiplyNode, "Multiply")

	    MultiplyNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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
	class MaskNode final : public Node
	{
	public:
	    MAT_NODE(MaskNode, "Mask")

	    MaskNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

	    std::span<PropertyBase*> GetProperties() override { return  m_properties; }

	private:
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
	
	class DotNode final : public Node
	{
	public:
	    MAT_NODE(DotNode, "Dot")

	    DotNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;
		
	private:
	    MaterialPin m_input1 = { this, "A" };
		MaterialPin m_input2 = { this, "B" };
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[2] = { &m_input1, &m_input2 };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	class OneMinusNode final : public Node
	{
	public:
	    MAT_NODE(OneMinusNode, "One Minus")

	    OneMinusNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;
		
	private:
	    MaterialPin m_input = { this };
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[1] = { &m_input };
	    Pin* m_OutputPins[1] = { &m_output };
	};

	class ClampNode final : public Node
	{
	public:
	    MAT_NODE(ClampNode, "Clamp")

	    ClampNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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

	class PowerNode final : public Node
	{
	public:
	    MAT_NODE(PowerNode, "Power")

	    PowerNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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
	class LerpNode final : public Node
	{
	public:
	    MAT_NODE(LerpNode, "Lerp")

	    LerpNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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
	class TextureCoordNode final : public Node
	{
	public:
		static const uint32 Color;

	public:
	    MAT_NODE(TextureCoordNode, "TexCoord")

	    TextureCoordNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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
	class WorldPositionNode final : public Node
	{
	public:
	    MAT_NODE(WorldPositionNode, "World Position")

	    WorldPositionNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return TextureCoordNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;
		
	private:
	    /// @brief The uv output pin.
	    MaterialPin m_coordinates = { this, "\0" };

	    /// @brief List of output pins as an array.
	    Pin* m_outputPins[1] = { &m_coordinates };
	};
	
	/// @brief A node which provides a pixel's world position as expression.
	class VertexNormalNode final : public Node
	{
	public:
	    MAT_NODE(VertexNormalNode, "Vertex Normal")

	    VertexNormalNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return TextureCoordNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;
		
	private:
	    /// @brief The uv output pin.
	    MaterialPin m_coordinates = { this };

	    /// @brief List of output pins as an array.
	    Pin* m_outputPins[1] = { &m_coordinates };
	};
	
	class AbsNode final : public Node
	{
	public:
	    MAT_NODE(AbsNode, "Abs")

	    AbsNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;
		
	private:
	    MaterialPin m_input = { this };
	    MaterialPin m_output = { this };
		
	    Pin* m_inputPins[1] = { &m_input };
	    Pin* m_OutputPins[1] = { &m_output };
	};
	
	/// @brief A node which adds an expression multiplication expression.
	class DivideNode final : public Node
	{
	public:
	    MAT_NODE(DivideNode, "Divide")

	    DivideNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
		
	    std::span<Pin*> GetOutputPins() override { return m_OutputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return ConstFloatNode::Color; }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

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
	
	/// @brief A node which adds a texture sample expression.
	class TextureNode final : public Node
	{
		static const uint32 Color;

	public:
	    MAT_NODE(TextureNode, "Texture")

	    TextureNode(MaterialGraph& material)
			: Node(material)
		{}
		
	    std::span<Pin*> GetInputPins() override { return m_inputPins; }
	    std::span<Pin*> GetOutputPins() override { return m_outputPins; }
		
		[[nodiscard]] uint32 GetColor() override { return Color; }

		[[nodiscard]] std::string_view GetTexture() const { return m_texturePath.GetPath(); }
		
		void SetTexture(const std::string_view texture) { m_texturePath.SetPath(texture); }

		ExpressionIndex Compile(MaterialCompiler& compiler) override;

		std::span<PropertyBase*> GetProperties() override { return m_properties; }

	private:
		AssetPathValue m_texturePath { "", ".htex" };
		AssetPathProperty m_texturePathProp { "Texture", m_texturePath };

		PropertyBase* m_properties[1] = { &m_texturePathProp };

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
}
