#include "model_frame.h"
#include "mesh_manager.h"

namespace mmo
{
	ModelFrame::ModelFrame(const std::string & name)
		: Frame("Model", name)
	{
		// Register default properties and subscribe to their Changed events.
		auto& fileProp = AddProperty("ModelFile", "");
		m_propConnections += fileProp.Changed.connect(this, &ModelFrame::OnModelFileChanged);
	}

	void ModelFrame::SetModelFile(const std::string & filename)
	{
		auto* prop = GetProperty("ModelFile");
		if (prop != nullptr)
		{
			prop->Set(filename);
		}
	}

	void ModelFrame::OnModelFileChanged(const Property& prop)
	{
		// Load the mesh file
		m_mesh = MeshManager::Get().Load(prop.GetValue());

		// Invalidate the frame
		Invalidate(false);
	}
}
