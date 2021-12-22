#pragma once

#include "base/non_copyable.h"
#include "frame_ui/color.h"
#include "graphics/graphics_device.h"

namespace mmo
{
	/// Base class of a render operation.
	class RenderOperation : public NonCopyable
	{
	public:
		/// Creates a new instance of the RenderOperation class and initializes it.
		///	@param device The graphics device used to create gpu resources.
		RenderOperation(GraphicsDevice& device)
			: m_device(device)
		{
		}

		/// Virtual default destructor because of inheritance.
		virtual ~RenderOperation() override = default;

	public:
		/// Gets the topology type to use when rendering this operation. Must be implemented.
		[[nodiscard]] virtual TopologyType GetTopologyType() const noexcept = 0;

		/// Gets the vertex format used when rendering this operation. Must be implemented.
		[[nodiscard]] virtual VertexFormat GetFormat() const noexcept = 0;

		/// Creates the gpu resources used for rendering this operation, like the vertex and/or
		///	index buffer. If no index buffer is generated, only the vertex buffer is used. Must
		///	be implemented.
		virtual void Finish() = 0;

	public:
		/// Renders the operation using the graphics device provided when the operation was created.
		void Render() const
		{
			ASSERT(m_vertexBuffer && "No vertex buffer created, did you call Finish before rendering?");

			m_device.SetTopologyType(GetTopologyType());
			m_device.SetVertexFormat(GetFormat());

			m_vertexBuffer->Set();

			if (m_indexBuffer)
			{
				m_indexBuffer->Set();
				m_device.DrawIndexed();
			}
			else
			{
				m_device.Draw(m_vertexBuffer->GetVertexCount());
			}
		}

	protected:
		GraphicsDevice& m_device;
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
	};

	/// Wrapper class for a RenderOperation which ensures that the Finish method is called
	///	once this wrapper class is deleted. Typical use case is to keep this object on the
	///	stack.
	template<class T>
	class RenderOperationRef final : public NonCopyable
	{
	public:
		/// Creates a new instance of the 
		RenderOperationRef(T& operation)
			: m_operation(&operation)
		{
		}

		/// Move operator.
		RenderOperationRef(RenderOperationRef&& other) noexcept
		{
			m_operation = other.m_operation;
			other.m_operation = nullptr;
		}

		/// Overridden destructor
		~RenderOperationRef() override
		{
			if (m_operation)
			{
				m_operation->Finish();	
			}
		}

	public:
		T* operator->() const noexcept
		{
			return m_operation;
		}

	private:
		T* m_operation;
	};

	/// A special operation which renders a list of lines for a ManualRenderObject.
	class LineListOperation final : public RenderOperation
	{
	public:
		/// Contains data for a single line in a line list operation.
		class Line final
		{
		public:
			/// Creates a new instance of the Line class and initializes it. The color of the line will
			///	be set to opaque white by default.
			///	@param start The start position of the line.
			///	@param end The end position of the line.
			Line(const Vector3& start, const Vector3& end) noexcept
				: m_start(start)
				, m_end(end)
				, m_startColor(0xffffffff)
				, m_endColor(0xffffffff)
			{
			}

		public:
			/// Sets the color for the entire line.
			///	@param color The new color value as argb value.
			void SetColor(const uint32 color) noexcept
			{
				m_startColor = m_endColor = color;
			}

			/// Sets the color for the start point of the line.
			///	@param color The new color value as argb value.
			void SetStartColor(const uint32 color) noexcept
			{
				m_startColor = color;
			}

			/// Sets the color for the end point of the line.
			///	@param color The new color value as argb value.
			void SetEndColor(const uint32 color) noexcept
			{
				m_endColor = color;
			}

			/// Gets the start position of the line.
			[[nodiscard]] const Vector3& GetStartPosition() const noexcept { return m_start; }

			/// Gets the end position of the line.
			[[nodiscard]] const Vector3& GetEndPosition() const noexcept { return m_end; }

			/// Gets the start color of the line.
			[[nodiscard]] uint32 GetStartColor() const noexcept { return m_startColor; }

			/// Gets the end color of the line.
			[[nodiscard]] uint32 GetEndColor() const noexcept { return m_endColor; }
			
		private:
			Vector3 m_start;
			Vector3 m_end;
			uint32 m_startColor { 0 };
			uint32 m_endColor { 0 };
		};
		
	public:
		/// Creates a new instance of the LineListOperation and initializes it.
		/// @param device The graphics device used to create gpu resources.
		explicit LineListOperation(GraphicsDevice& device)
			: RenderOperation(device)
		{
		}

	public:
		/// @copydoc RenderOperation::GetTopologyType
		[[nodiscard]] TopologyType GetTopologyType() const noexcept override
		{
			return TopologyType::LineList;
		}

		/// @copydoc RenderOperation::GetFormat
		[[nodiscard]] VertexFormat GetFormat() const noexcept override
		{
			return VertexFormat::PosColor;
		}

		/// @copydoc RenderOperation::Finish
		void Finish() override
		{
			ASSERT(!m_lines.empty() && "At least one line has to be added!");

			std::vector<POS_COL_VERTEX> vertices;
			vertices.reserve(m_lines.size() * 2);

			for(auto& line : m_lines)
			{
				const POS_COL_VERTEX v1 { line.GetStartPosition(), line.GetStartColor() };
				vertices.emplace_back(v1);

				const POS_COL_VERTEX v2 { line.GetEndPosition(), line.GetEndColor() };
				vertices.emplace_back(v2);
			}

			m_vertexBuffer = m_device.CreateVertexBuffer(vertices.size(), sizeof(POS_COL_VERTEX), false, vertices.data());
		}

	public:
		/// Adds a new line to the operation. The line will have a default color of white.
		Line& AddLine(const Vector3& start, const Vector3& end)
		{
			m_lines.emplace_back(start, end);
			return m_lines.back();
		}
		
	private:
		/// A list of lines to render.
		std::vector<Line> m_lines;
	};

	/// A class which helps rendering manually (at runtime) created objects so you don't have to mess
	///	around with the low level graphics api.
	class ManualRenderObject final
	{
	public:
		/// Creates a new instance of the ManualRenderObject class and initializes it.
		///	@param device The graphics device object to use for gpu resource creation and rendering.
		explicit ManualRenderObject(GraphicsDevice& device);

	public:
		/// Adds a new render operation to the object which draws a line list.
		RenderOperationRef<LineListOperation> AddLineListOperation();

		/// Removes all operations.
		void Clear() noexcept;

		/// Renders all operations which were added to the object.
		void Render() const;

	private:
		GraphicsDevice& m_device;
		std::vector<std::unique_ptr<RenderOperation>> m_operations;
	};
}
