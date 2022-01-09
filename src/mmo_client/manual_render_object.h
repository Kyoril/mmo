// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "frame_ui/color.h"
#include "graphics/graphics_device.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/render_queue.h"

namespace mmo
{
	class ManualRenderObject;

	/// Base class of a render operation.
	class ManualRenderOperation : public NonCopyable, public Renderable
	{
	public:
		/// Creates a new instance of the RenderOperation class and initializes it.
		///	@param device The graphics device used to create gpu resources.
		///	@param parent The parent object where this operation belongs to.
		ManualRenderOperation(GraphicsDevice& device, ManualRenderObject& parent)
			: m_device(device)
			, m_parent(parent)
		{
		}

		/// Virtual default destructor because of inheritance.
		virtual ~ManualRenderOperation() override = default;

	protected:
		/// Gets the topology type to use when rendering this operation. Must be implemented.
		[[nodiscard]] virtual TopologyType GetTopologyType() const noexcept = 0;

		/// Gets the vertex format used when rendering this operation. Must be implemented.
		[[nodiscard]] virtual VertexFormat GetFormat() const noexcept = 0;

	public:
		/// Creates the gpu resources used for rendering this operation, like the vertex and/or
		///	index buffer. If no index buffer is generated, only the vertex buffer is used. Must
		///	be implemented.
		virtual void Finish();

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

		void PrepareRenderOperation(RenderOperation& operation) override;
		[[nodiscard]] const Matrix4& GetWorldTransform() const override;
		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;
		[[nodiscard]] virtual const AABB& GetBoundingBox() const noexcept = 0;

	protected:
		GraphicsDevice& m_device;
		ManualRenderObject& m_parent;
		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
	};

	/// Wrapper class for a RenderOperation which ensures that the Finish method is called
	///	once this wrapper class is deleted. Typical use case is to keep this object on the
	///	stack.
	template<class T>
	class ManualRenderOperationRef final : public NonCopyable
	{
	public:
		/// Creates a new instance of the 
		ManualRenderOperationRef(T& operation)
			: m_operation(&operation)
		{
		}

		/// Move operator.
		ManualRenderOperationRef(ManualRenderOperationRef&& other) noexcept
		{
			m_operation = other.m_operation;
			other.m_operation = nullptr;
		}

		/// Overridden destructor
		~ManualRenderOperationRef() override
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
	class ManualLineListOperation final : public ManualRenderOperation
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
		///	@param parent The parent object where this operation belongs to.
		explicit ManualLineListOperation(GraphicsDevice& device, ManualRenderObject& parent)
			: ManualRenderOperation(device, parent)
		{
		}

	protected:
		/// @copydoc ManualRenderOperation::GetTopologyType
		[[nodiscard]] TopologyType GetTopologyType() const noexcept override
		{
			return TopologyType::LineList;
		}

		/// @copydoc ManualRenderOperation::GetFormat
		[[nodiscard]] VertexFormat GetFormat() const noexcept override
		{
			return VertexFormat::PosColor;
		}

	public:
		/// @copydoc ManualRenderOperation::Finish
		void Finish() override
		{
			ASSERT(!m_lines.empty() && "At least one line has to be added!");
			
			std::vector<POS_COL_VERTEX> vertices;
			vertices.reserve(m_lines.size() * 2);

			bool firstLine = true;

			for(auto& line : m_lines)
			{

				const POS_COL_VERTEX v1 { line.GetStartPosition(), line.GetStartColor() };
				vertices.emplace_back(v1);

				const POS_COL_VERTEX v2 { line.GetEndPosition(), line.GetEndColor() };
				vertices.emplace_back(v2);
				
				if (firstLine)
				{
					m_boundingBox.min = TakeMinimum(line.GetStartPosition(), line.GetEndPosition());
					m_boundingBox.max = TakeMaximum(line.GetStartPosition(), line.GetEndPosition());
					firstLine = false;
				}
				else
				{
					m_boundingBox.min = TakeMinimum(m_boundingBox.min, line.GetStartPosition());
					m_boundingBox.min = TakeMinimum(m_boundingBox.min, line.GetEndPosition());
					m_boundingBox.max = TakeMaximum(m_boundingBox.max, line.GetStartPosition());
					m_boundingBox.max = TakeMaximum(m_boundingBox.max, line.GetEndPosition());
				}
			}

			m_vertexBuffer = m_device.CreateVertexBuffer(vertices.size(), sizeof(POS_COL_VERTEX), false, vertices.data());
			
			ManualRenderOperation::Finish();
		}

	public:
		/// Adds a new line to the operation. The line will have a default color of white.
		Line& AddLine(const Vector3& start, const Vector3& end)
		{
			m_lines.emplace_back(start, end);
			return m_lines.back();
		}

		[[nodiscard]] const AABB& GetBoundingBox() const noexcept override { return m_boundingBox; }

	private:
		/// A list of lines to render.
		std::vector<Line> m_lines;
		AABB m_boundingBox;
	};

	/// A class which helps rendering manually (at runtime) created objects so you don't have to mess
	///	around with the low level graphics api.
	class ManualRenderObject final : public MovableObject
	{
		friend class ManualRenderOperation;

	public:
		/// Creates a new instance of the ManualRenderObject class and initializes it.
		///	@param device The graphics device object to use for gpu resource creation and rendering.
		explicit ManualRenderObject(GraphicsDevice& device);

	public:
		/// Adds a new render operation to the object which draws a line list.
		ManualRenderOperationRef<ManualLineListOperation> AddLineListOperation();

		/// Removes all operations.
		void Clear() noexcept;

	public:
		/// @brief Sets the render queue group id, which controls when exactly this object will be rendered.
		/// @param renderQueueGroupId The render queue group id of this object.
		void SetRenderQueueGroupId(uint8 renderQueueGroupId);

		/// @brief Sets the priority of this object in it's render queue group.
		///	       The higher the priority, the earlier it will be rendered.
		/// @param priority The priority of this object in the render queue group.
		void SetRenderQueueGroupPriority(uint16 priority);

	public:
		/// @copydoc MovableObject::GetMovableType
		[[nodiscard]] const String& GetMovableType() const override;

		/// @copydoc MovableObject::GetBoundingBox
		[[nodiscard]] const AABB& GetBoundingBox() const override { return m_worldAABB; }
		
		/// @copydoc MovableObject::GetBoundingRadius
		[[nodiscard]] float GetBoundingRadius() const override { return m_boundingRadius; }
		
		/// @copydoc MovableObject::VisitRenderables
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;
		
		/// @copydoc MovableObject::PopulateRenderQueue
		void PopulateRenderQueue(RenderQueue& queue) override;

	private:
		void NotifyOperationUpdated();

	private:
		GraphicsDevice& m_device;
		std::vector<std::unique_ptr<ManualRenderOperation>> m_operations;
		AABB m_worldAABB;
		float m_boundingRadius { 0.0f };
		uint8 m_renderQueueGroupId { Main };
		uint16 m_renderQueueGroupPriority { 0 };
	};
}
