// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "fbx_import.h"

#include "log/default_log_levels.h"


#ifdef IOS_REF
#	undef  IOS_REF
#	define IOS_REF (*(m_sdkManager->GetIOSettings()))
#endif

namespace mmo
{
	FbxImport::FbxImport()
	{
		InitializeSdkObjects();
	}

	FbxImport::~FbxImport()
	{
		DestroySdkObjects();
	}

	bool FbxImport::LoadScene(const char * pFilename)
	{
		// Remove existing mesh entries
		m_meshEntries.clear();

		// Clear scene
		m_scene->Clear();

		int lFileMajor, lFileMinor, lFileRevision;
		int lSDKMajor, lSDKMinor, lSDKRevision;
		//int lFileFormat = -1;
		int i, lAnimStackCount;
		bool lStatus;

		// Get the file version number generate by the FBX SDK.
		FbxManager::GetFileFormatVersion(lSDKMajor, lSDKMinor, lSDKRevision);

		// Create an importer.
		FbxImporter* lImporter = FbxImporter::Create(m_sdkManager, "");

		// Initialize the importer by providing a filename.
		const bool lImportStatus = lImporter->Initialize(pFilename, -1, m_sdkManager->GetIOSettings());
		lImporter->GetFileVersion(lFileMajor, lFileMinor, lFileRevision);

		if (!lImportStatus)
		{
			FbxString error = lImporter->GetStatus().GetErrorString();
			ELOG("Call to FbxImporter::Initialize() failed.");
			ELOG("Error returned: " << error.Buffer());

			if (lImporter->GetStatus().GetCode() == FbxStatus::eInvalidFileVersion)
			{
				ELOG("FBX file format version for this FBX SDK is " << lSDKMajor << "." << lSDKMinor << "." << lSDKRevision);
				ELOG("FBX file format version for file '" << pFilename << "' is " << lFileMajor << "." << lFileMinor << "." << lFileRevision);
			}

			return false;
		}

		ILOG("FBX file format version for this FBX SDK is " << lSDKMajor << "." << lSDKMinor << "." << lSDKRevision);

		if (lImporter->IsFBX())
		{
			ILOG("FBX file format version for file '" << pFilename << "' is " << lFileMajor << "." << lFileMinor << "." << lFileRevision);

			// From this point, it is possible to access animation stack information without
			// the expense of loading the entire file.

			ILOG("Animation Stack Information");

			lAnimStackCount = lImporter->GetAnimStackCount();

			ILOG("    Number of Animation Stacks: " << lAnimStackCount);
			ILOG("    Current Animation Stack: \"" << lImporter->GetActiveAnimStackName().Buffer() << "\"");
			ILOG("");

			for (i = 0; i < lAnimStackCount; i++)
			{
				FbxTakeInfo* lTakeInfo = lImporter->GetTakeInfo(i);

				ILOG("    Animation Stack " << i);
				ILOG("         Name: \"" << lTakeInfo->mName.Buffer() << "\"");
				ILOG("         Description: \"" << lTakeInfo->mDescription.Buffer() << "\"");

				// Change the value of the import name if the animation stack should be imported 
				// under a different name.
				ILOG("         Import Name: \"" << lTakeInfo->mImportName.Buffer() << "\"");

				// Set the value of the import state to false if the animation stack should be not
				// be imported. 
				ILOG("         Import State: " << (lTakeInfo->mSelect ? "true" : "false"));
				ILOG("");
			}

			// Set the import states. By default, the import states are always set to 
			// true. The code below shows how to change these states.
			IOS_REF.SetBoolProp(IMP_FBX_MATERIAL, true);
			//IOS_REF.SetBoolProp(IMP_FBX_TEXTURE, true);
			//IOS_REF.SetBoolProp(IMP_FBX_LINK, true);
			IOS_REF.SetBoolProp(IMP_FBX_SHAPE, true);
			IOS_REF.SetBoolProp(IMP_FBX_GOBO, true);
			//IOS_REF.SetBoolProp(IMP_FBX_ANIMATION, true);
			IOS_REF.SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
		}

		// Import the scene.
		lStatus = lImporter->Import(m_scene);

		if (lStatus == false && lImporter->GetStatus().GetCode() == FbxStatus::ePasswordError)
		{
			ELOG("FBX file is protected by a password, won't import file!");
		}

		// Destroy the importer.
		lImporter->Destroy();

		// Create a converter
		FbxGeometryConverter converter(m_sdkManager);
		converter.Triangulate(m_scene, true);

		// Find root node
		FbxNode* rootNode = m_scene->GetRootNode();
		if (rootNode == nullptr)
		{
			ELOG("Fbx file has no root node!");
			return 1;
		}

		// Traverse through all nodes
		TraverseScene(*rootNode);

		return lStatus;
	}

	void FbxImport::InitializeSdkObjects()
	{
		// The first thing to do is to create the FBX Manager which is the object allocator for almost all the classes in the SDK
		m_sdkManager = FbxManager::Create();
		ASSERT(m_sdkManager);

		ILOG("Autodesk FBX SDK version " << m_sdkManager->GetVersion());

		// Create an IOSettings object. This object holds all import/export settings.
		FbxIOSettings* ios = FbxIOSettings::Create(m_sdkManager, IOSROOT);
		m_sdkManager->SetIOSettings(ios);

		//Create an FBX scene. This object holds most objects imported/exported from/to files.
		m_scene = FbxScene::Create(m_sdkManager, "My Scene");
		if (!m_scene)
		{
			ELOG("Error: Unable to create FBX scene!");
		}
	}

	void FbxImport::DestroySdkObjects()
	{
		//Delete the FBX Manager. All the objects that have been allocated using the FBX Manager and that haven't been explicitly destroyed are also automatically destroyed.
		if (m_sdkManager) m_sdkManager->Destroy();
	}

	void FbxImport::TraverseScene(FbxNode & node)
	{
		// Find mesh nodes
		const FbxMesh* mesh = node.GetMesh();
		if (mesh)
		{
			ILOG("Geometry Node: " << node.GetName());

			// Create a new mesh entry
			mmo::MeshEntry entry;
			entry.name = node.GetName();
			entry.maxIndex = 0;

			// Gets the control points of the mesh
			FbxVector4* vertices = mesh->GetControlPoints();
			int vertexCount = mesh->GetControlPointsCount();
			ILOG("\tVertices: " << vertexCount);

			if (vertexCount > 0)
			{
				// Copy it's vertex data
				entry.vertices.reserve(vertexCount);
				for (int iVert = 0; iVert < vertexCount; ++iVert)
				{
					mmo::Vertex vertex;

					// Grab the vertex position data
					FbxVector4 vertexPos = vertices[iVert];
					vertex.position = mmo::Vector3(vertexPos[0], vertexPos[1], vertexPos[2]);

					// TODO: Grab vertex normal and texture coordinates

					// TODO: Grab vertex color?
					vertex.color = 0xffffffff;

					// Save vertex
					entry.vertices.push_back(vertex);
				}

				// Index count for the meshes geometry
				int* indices = mesh->GetPolygonVertices();
				int indexCount = mesh->GetPolygonCount() * 3;
				ILOG("\tTriangles: " << (indexCount / 3));

				if (indexCount >= 3)
				{
					// Output all faces for testing
					for (int iInd = 0; iInd < indexCount; iInd++)
					{
						// Negative index?
						if (indices[iInd] < 0)
						{
							WLOG("Negative index found in mesh node '" << entry.name << "'!");
							break;
						}

						entry.indices.push_back(indices[iInd]);
						if (static_cast<uint32>(indices[iInd]) > entry.maxIndex)
						{
							entry.maxIndex = indices[iInd];
						}
					}

					// Check that all indices were valid / have been added
					if (entry.indices.size() == indexCount)
					{
						// Save mesh entry
						m_meshEntries.push_back(entry);
					}
				}
			}
		}

		// Recursively traverse child nodes
		const int childCount = node.GetChildCount();
		for (int i = 0; i < childCount; ++i)
		{
			TraverseScene(*node.GetChild(i));
		}
	}
}