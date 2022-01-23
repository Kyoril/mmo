// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "fbx_import.h"

#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "frame_ui/color.h"
#include "graphics/graphics_device.h"
#include "graphics/vertex_format.h"
#include "log/default_log_levels.h"
#include "mesh_v1_0/header.h"
#include "mesh_v1_0/header_save.h"


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
		
		//FbxAxisSystem::DirectX.ConvertScene(m_scene);

		// Create a converter
		FbxGeometryConverter converter(m_sdkManager);
		if (!converter.Triangulate(m_scene, true))
		{
			ELOG("Failed to triangulate mesh");
		}
		
		// Find root node
		FbxNode* rootNode = m_scene->GetRootNode();
		if (rootNode == nullptr)
		{
			ELOG("Fbx file has no root node!");
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
	
    FbxColor GetFBXColor(FbxMesh *pMesh, const int polyIndex, const int polyPointIndex)
    {
        int lControlPointIndex = pMesh->GetPolygonVertex(polyIndex, polyPointIndex);
        int vertexId = polyIndex * 3 + polyPointIndex;

        FbxColor color;
        for (int l = 0; l < pMesh->GetElementVertexColorCount(); l++)
        {
            FbxGeometryElementVertexColor* leVtxc = pMesh->GetElementVertexColor( l);

            switch (leVtxc->GetMappingMode())
            {
            case FbxGeometryElement::eByControlPoint:
                switch (leVtxc->GetReferenceMode())
                {
                case FbxGeometryElement::eDirect:
                    color = leVtxc->GetDirectArray().GetAt(lControlPointIndex);
                    break;
                case FbxGeometryElement::eIndexToDirect:
                    {
                        int id = leVtxc->GetIndexArray().GetAt(lControlPointIndex);
                        color = leVtxc->GetDirectArray().GetAt(id);
                    }
                    break;
                default:
                    break; // other reference modes not shown here!
                }
                break;

            case FbxGeometryElement::eByPolygonVertex:
                {
                    switch (leVtxc->GetReferenceMode())
                    {
                    case FbxGeometryElement::eDirect:
                        color = leVtxc->GetDirectArray().GetAt(vertexId);
                        break;
                    case FbxGeometryElement::eIndexToDirect:
                        {
                            int id = leVtxc->GetIndexArray().GetAt(vertexId);
                            color = leVtxc->GetDirectArray().GetAt(id);
                        }
                        break;
                    default:
                        break; // other reference modes not shown here!
                    }
                }
                break;

            case FbxGeometryElement::eByPolygon: // doesn't make much sense for UVs
            case FbxGeometryElement::eAllSame:   // doesn't make much sense for UVs
            case FbxGeometryElement::eNone:       // doesn't make much sense for UVs
                break;
            }
        }
        return color;
    }

	int GetUVSets(FbxNode *pNode, FbxMesh *pMesh)
    {
		int numUvSets = 0;

        if( pNode && pMesh) 
        {
            for( int i = 0; i < pMesh->GetElementUVCount(); ++i )
            {
	            const FbxGeometryElementUV* uvElement = pMesh->GetElementUV(i);
				if (uvElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					++numUvSets;
				}
            }
        }

        return numUvSets;
    }

	void FbxImport::TraverseScene(FbxNode & node)
	{
		// Find mesh nodes
		FbxMesh* mesh = node.GetMesh();
		if (mesh)
		{
			if( mesh->CheckIfVertexNormalsCCW() )
            {
                WLOG("Counter clockwise normals detected.");
            }

            if(mesh->GetLayer(0)->GetNormals() == nullptr)
            {
                ILOG("Recomputing normals...");
                mesh->InitNormals();
                mesh->GenerateNormals();
            }

			ILOG("Geometry Node: " << node.GetName());
			
			// Create a new mesh entry
			mmo::MeshEntry entry;
			entry.name = node.GetName();
			entry.maxIndex = 0;
			
			const auto& transform = node.EvaluateGlobalTransform();
			
			FbxVector4* vertices = mesh->GetControlPoints();
			
			const int uvSetCount = GetUVSets(&node, mesh);
			ILOG("Number of UV sets: " << uvSetCount);

			// Gets the control points of the mesh
			const auto polyCount = mesh->GetPolygonCount();
			entry.vertices.reserve(mesh->GetControlPointsCount());
			ILOG("\tTriangles: " << polyCount);

			for (int face = 0; face < polyCount; ++face)
			{
				for (int vertex = 0; vertex < 3; ++vertex)
				{
					const int vertexId = mesh->GetPolygonVertex(face, vertex);

					mmo::Vertex v;

					// Get position
					FbxVector4 vertexPos = transform.MultT(vertices[vertexId]);

					// Get vertex color
					const auto vertexColor = GetFBXColor(mesh, face, vertexId);

					// Get UVW
					if (uvSetCount > 0)
					{
						int uvIndex = mesh->GetTextureUVIndex(face, vertexId);
						FbxGeometryElementUV* uv = mesh->GetElementUV(0);
						FbxVector2 uvVector = uv->GetDirectArray().GetAt(uvIndex);
						v.texCoord = Vector3(uvVector[0], 1.0f - uvVector[1], 0.0f);
					}
					else
					{
						v.texCoord = Vector3::Zero;
					}
					
					v.position = mmo::Vector3(vertexPos[0], vertexPos[1], vertexPos[2]);
					v.color = Color(vertexColor.mRed, vertexColor.mGreen, vertexColor.mBlue, vertexColor.mAlpha);
					v.normal = Vector3::UnitZ;

					entry.vertices.emplace_back(std::move(v));
					entry.indices.push_back(entry.indices.size() - 1);

					if (static_cast<uint32>(vertexId) > entry.maxIndex)
					{
						entry.maxIndex = vertexId;
					}
				}
			}

			ILOG("\tVertices: " << entry.vertices.size());
			
			// Save mesh entry
			m_meshEntries.push_back(entry);
		}

		// Recursively traverse child nodes
		const int childCount = node.GetChildCount();
		for (int i = 0; i < childCount; ++i)
		{
			TraverseScene(*node.GetChild(i));
		}
	}

	bool FbxImport::SaveMeshFile(const String& filename, const Path& assetPath)
	{
		std::filesystem::path p = (assetPath / filename).string() + ".hmsh";
		
		// Create the file name
		const auto filePtr = AssetRegistry::CreateNewFile(p.string());
		if (filePtr == nullptr)
		{
			ELOG("Unable to create mesh file " << p);
			return false;
		}

		// Setup mesh header
		mesh::v1_0::Header header;
		header.version = mesh::Version_1_0;
		header.vertexChunkOffset = 0;
		header.indexChunkOffset = 0;
		
		// Create a mesh header saver
		io::StreamSink sink{ *filePtr };
		io::Writer writer{ sink };

		// Write the mesh header
		mesh::v1_0::HeaderSaver saver{ sink, header };
		{
			// Write the vertex chunk data
			header.vertexChunkOffset = static_cast<uint32>(sink.Position());
			ChunkWriter vertexChunkWriter{ mesh::v1_0::VertexChunkMagic, writer };
			{
				const auto& meshes = m_meshEntries;
				if (!meshes.empty())
				{
					const auto& mesh = meshes.front();

					// Write vertex data
					writer << io::write<uint32>(mesh.vertices.size());
					for (size_t i = 0; i < mesh.vertices.size(); ++i)
					{
						writer
							<< io::write<float>(mesh.vertices[i].position.x)
							<< io::write<float>(mesh.vertices[i].position.y)
							<< io::write<float>(mesh.vertices[i].position.z);
						writer
							<< io::write<uint32>(mesh.vertices[i].color);
						writer
							<< io::write<float>(mesh.vertices[i].texCoord.x)
							<< io::write<float>(mesh.vertices[i].texCoord.y)
							<< io::write<float>(mesh.vertices[i].texCoord.z);
						writer
							<< io::write<float>(mesh.vertices[i].normal.x)
							<< io::write<float>(mesh.vertices[i].normal.y)
							<< io::write<float>(mesh.vertices[i].normal.z);
					}
				}
			}
			vertexChunkWriter.Finish();

			// Write the index chunk data
			header.indexChunkOffset = static_cast<uint32>(sink.Position());
			ChunkWriter indexChunkWriter{ mesh::v1_0::IndexChunkMagic, writer };
			{
				const auto& meshes = m_meshEntries;
				if (!meshes.empty())
				{
					const auto& mesh = meshes.front();
					const bool bUse16BitIndices = mesh.vertices.size() <= std::numeric_limits<uint16>().max();
					
					// Write index data
					writer
						<< io::write<uint32>(mesh.indices.size())
						<< io::write<uint8>(bUse16BitIndices);

					for (size_t i = 0; i < mesh.indices.size(); ++i)
					{
						if (bUse16BitIndices)
						{
							writer << io::write<uint16>(mesh.indices[i]);
						}
						else
						{
							writer << io::write<uint32>(mesh.indices[i]);
						}
					}
				}
			}
			indexChunkWriter.Finish();
		}
		saver.Finish();
		return true;
	}

	bool FbxImport::ImportFromFile(const Path& filename, const Path& currentAssetPath)
	{
		if (!LoadScene(filename.string().c_str()))
		{
			return false;
		}

		// TODO: Change this, but for now we will create a vertex and index buffer from the first mesh that was found
		if (m_meshEntries.empty())
		{
			WLOG("FBX has no geometry data!");
			return false;
		}

		return SaveMeshFile(filename.filename().replace_extension().string(), currentAssetPath);
	}

	bool FbxImport::SupportsExtension(const String& extension) const noexcept
	{
		return extension == ".fbx";
	}
}
