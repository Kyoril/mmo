// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "fbx_import.h"

#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "frame_ui/color.h"
#include "graphics/graphics_device.h"
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

	bool FbxImport::LoadScene(const String& filename)
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
		const bool lImportStatus = lImporter->Initialize(filename.c_str(), -1, m_sdkManager->GetIOSettings());
		lImporter->GetFileVersion(lFileMajor, lFileMinor, lFileRevision);

		if (!lImportStatus)
		{
			FbxString error = lImporter->GetStatus().GetErrorString();
			ELOG("Call to FbxImporter::Initialize() failed.");
			ELOG("Error returned: " << error.Buffer());

			if (lImporter->GetStatus().GetCode() == FbxStatus::eInvalidFileVersion)
			{
				ELOG("FBX file format version for this FBX SDK is " << lSDKMajor << "." << lSDKMinor << "." << lSDKRevision);
				ELOG("FBX file format version for file '" << filename << "' is " << lFileMajor << "." << lFileMinor << "." << lFileRevision);
			}

			return false;
		}

		ILOG("FBX file format version for this FBX SDK is " << lSDKMajor << "." << lSDKMinor << "." << lSDKRevision);

		if (lImporter->IsFBX())
		{
			ILOG("FBX file format version for file '" << filename << "' is " << lFileMajor << "." << lFileMinor << "." << lFileRevision);

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
		else
		{
			// Traverse through all nodes
			TraverseScene(*rootNode);	
		}

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
		if (m_sdkManager) 
		{
			m_sdkManager->Destroy();
		}
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
	                    const int id = leVtxc->GetIndexArray().GetAt(lControlPointIndex);
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
	                        const int id = leVtxc->GetIndexArray().GetAt(vertexId);
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
	
	void FbxImport::TraverseScene(FbxNode & node)
	{
		const auto* attribute = node.GetNodeAttribute();
		if (attribute)
		{
			if (attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
			{
				auto* mesh = node.GetMesh();
				if (!mesh)
				{
					WLOG("Found mesh node without mesh, skipping node " << node.GetName());
				}
				else if (!LoadMesh(node, *mesh))
				{
					WLOG("Failed to load mesh node " << node.GetName() << ", skipping");
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

	bool FbxImport::SaveMeshFile(const String& filename, const Path& assetPath) const
	{
		const std::filesystem::path p = (assetPath / filename).string() + ".hmsh";
		
		// Create the file name
		const auto filePtr = AssetRegistry::CreateNewFile(p.string());
		if (filePtr == nullptr)
		{
			ELOG("Unable to create mesh file " << p);
			return false;
		}
		
		// Create a mesh header saver
		io::StreamSink sink{ *filePtr };
		io::Writer writer{ sink };

		MeshSerializer serializer;
		serializer.ExportMesh(m_meshEntries.front(), writer);

		return true;
	}

	bool FbxImport::LoadMeshPolygons(FbxMesh& mesh, MeshGeometry& geometry)
	{
		const auto* polygonVertices = mesh.GetPolygonVertices();
		if (!polygonVertices)
		{
			ELOG("Unable to get polygon vertices");
			return false;
		}

		const auto polyCount = mesh.GetPolygonCount();
		for (size_t polygon = 0; polygon < polyCount; ++polygon)
		{
			const auto polygonSize = mesh.GetPolygonSize(polygon);
			if (polygonSize != 3)
			{
				ELOG("Detected non-triangle polygon");
				return false;
			}

			for (size_t polygonVertex = 0; polygonVertex < polygonSize; ++polygonVertex)
			{
				const auto polygonVertexId = mesh.GetPolygonVertex(polygon, polygonVertex);
				if (polygonVertexId < 0)
				{
					ELOG("Detected polygon with invalid vertex id");
					return false;
				}

				geometry.polygonIndices.emplace_back(polygonVertexId);
			}
		}

		return true;
	}

	void FbxImport::GenerateMeshEntry(MeshEntry& entry, const MeshGeometry& geometry)
	{
		const bool allUvsByControlPoints = geometry.uvSets[0].uvs.size() == geometry.positions.size();
		const bool allNormalsByControlPoints = geometry.normals.size() == geometry.positions.size();
		const bool allAttributesByControlPoints = allUvsByControlPoints && allNormalsByControlPoints;
		if (allAttributesByControlPoints)
		{
			DLOG("Vertex attributes by control points");

			entry.vertices.resize(geometry.positions.size(), Vertex{});
			entry.indices.resize(geometry.polygonIndices.size(), 0);

			for (size_t i = 0; i < geometry.positions.size(); ++i)
			{
				entry.vertices[i].position = geometry.positions[i];
			}
			
			for (size_t i = 0; i < geometry.uvSets[0].uvs.size(); ++i)
			{
				entry.vertices[i].texCoord = geometry.uvSets[0].uvs[i];
			}

			for (size_t i = 0; i < geometry.normals.size(); ++i)
			{
				entry.vertices[i].normal = geometry.normals[i];
				entry.vertices[i].normal.Normalize();
			}

			for (size_t i = 0; i < geometry.polygonIndices.size(); ++i)
			{
				entry.indices[i] = geometry.polygonIndices[i];
				if (entry.indices[i] > entry.maxIndex)
				{
					entry.maxIndex = entry.indices[i];
				}
			}
		}
		else
		{
			DLOG("Vertex attributes by polygon vertex");

			entry.vertices.resize(geometry.uvSets[0].uvs.size(), Vertex{});
			entry.indices.resize(geometry.polygonIndices.size(), 0);

			for (size_t i = 0; i < geometry.polygonIndices.size(); ++i)
			{
				entry.vertices[i].position = geometry.positions[geometry.polygonIndices[i]];

				if (allNormalsByControlPoints)
				{
					entry.vertices[i].normal = geometry.normals[geometry.polygonIndices[i]];	
				}
				else
				{
					entry.vertices[i].normal = geometry.normals[i];
				}

				entry.vertices[i].normal.Normalize();
				
				if (allUvsByControlPoints)
				{
					entry.vertices[i].texCoord = geometry.uvSets[0].uvs[geometry.polygonIndices[i]];
				}
				else
				{
					entry.vertices[i].texCoord = geometry.uvSets[0].uvs[i];
				}
				
				entry.vertices[i].color = 0xffffffff;
				
				entry.indices[i] = i;

				if (entry.indices[i] > entry.maxIndex)
				{
					entry.maxIndex = entry.indices[i];
				}
			}
		}
	}

	void FbxImport::LoadMeshUvs(FbxMesh& mesh, MeshGeometry& geometry)
	{
		FbxStringList uvNames;
		mesh.GetUVSetNames(uvNames);

		const char* uvName = nullptr;
		if (uvNames.GetCount())
		{
			uvName = uvNames[0];
		}

		FbxGeometryElementUV* uv = mesh.GetElementUV(0);
		const auto mappingMode = uv->GetMappingMode();
		if (mappingMode == FbxLayerElement::eByControlPoint)
		{
			const auto lPolygonVertexCount = mesh.GetControlPointsCount();
			for (int lIndex = 0; lIndex < lPolygonVertexCount; ++lIndex)
			{
				int lUvIndex = lIndex;
				if (uv->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					lUvIndex = uv->GetIndexArray().GetAt(lIndex);
				}
				auto lCurrentUv = uv->GetDirectArray().GetAt(lUvIndex);
				geometry.uvSets[0].uvs.emplace_back(Vector3(lCurrentUv[0], 1.0f - lCurrentUv[1], 0.0f));
			}
		}
		else
		{
			const int polygonCount = mesh.GetPolygonCount();
			for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
			{
				for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
				{
					bool lUnmappedUV;
					FbxVector2 lCurrentUV;
					mesh.GetPolygonVertexUV(polygonIndex, vertexIndex, uvName, lCurrentUV, lUnmappedUV);
					geometry.uvSets[0].uvs.emplace_back(Vector3(lCurrentUV[0], 1.0f - lCurrentUV[1], 0.0f));
				}
			}
		}
	}

	void FbxImport::LoadMeshNormals(FbxNode& node, FbxMesh& mesh, MeshGeometry& geometry)
	{
		const auto transform = node.EvaluateGlobalTransform().Inverse().Transpose();

		auto* normalElem = mesh.GetElementNormal(0);
		const auto mappingMode = normalElem->GetMappingMode();
		if (mappingMode == FbxLayerElement::eByControlPoint)
		{
			const auto lPolygonVertexCount = mesh.GetControlPointsCount();
			for (int lIndex = 0; lIndex < lPolygonVertexCount; ++lIndex)
			{
				int lNormalIndex = lIndex;
				if (normalElem->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				{
					lNormalIndex = normalElem->GetIndexArray().GetAt(lIndex);
				}

				const auto lCurrentNormal = transform.MultT(normalElem->GetDirectArray().GetAt(lNormalIndex));
				geometry.normals.emplace_back(Vector3(lCurrentNormal[0], lCurrentNormal[1], lCurrentNormal[2]));
			}
		}
		else if (mappingMode != FbxLayerElement::eNone)
		{
			const int polygonCount = mesh.GetPolygonCount();
			for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
			{
				for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
				{
					FbxVector4 lCurrentNormal;
					mesh.GetPolygonVertexNormal(polygonIndex, vertexIndex, lCurrentNormal);
					
					lCurrentNormal = transform.MultT(lCurrentNormal);
					geometry.normals.emplace_back(Vector3(lCurrentNormal[0], lCurrentNormal[1], lCurrentNormal[2]));
				}
			}
		}
	}

	bool FbxImport::LoadMesh(FbxNode& node, FbxMesh& mesh)
	{
        if(mesh.GetLayer(0)->GetNormals() == nullptr)
        {
            ILOG("Mesh is missing normal information, recalculating normals using FBX SDK...");
            mesh.InitNormals();
            mesh.GenerateNormals(true, true, true);
        }
		
		if(mesh.CheckIfVertexNormalsCCW())
        {
            WLOG("Counter clockwise normals detected.");
        }

		ILOG("Processing mesh node " << node.GetName() << "...");
		
		// Create a new mesh entry
		MeshEntry entry {};
		entry.name = node.GetName();
		entry.maxIndex = 0;

		MeshGeometry geometry{};

		// Determine number of uv sets for mesh and reserve uv set memory
		if (!InitializeUvSets(mesh, geometry))
		{
			return false;
		}

		// Load positions (node is required to transform the vertex positions accordingly. A mesh can be assigned to multiple nodes!)
		if (!LoadMeshVertexPositions(node, mesh, geometry))
		{
			return false;
		}

		// Load colors
		

		// Load normals
		LoadMeshNormals(node, mesh, geometry);

		// Load UVs
		LoadMeshUvs(mesh, geometry);

		// Load polygon indices
        if (!LoadMeshPolygons(mesh, geometry))
        {
	        return false;
        }
		
		FbxLayerElementArrayTemplate<int>* lMaterialIndice = nullptr;
		FbxGeometryElement::EMappingMode lMaterialMappingMode = FbxGeometryElement::eNone;
		if (mesh.GetElementMaterial())
		{
			lMaterialIndice = &mesh.GetElementMaterial()->GetIndexArray();
			lMaterialMappingMode = mesh.GetElementMaterial()->GetMappingMode();
			if (lMaterialIndice && lMaterialMappingMode == FbxGeometryElement::eByPolygon)
			{
				const int lPolygonCount = mesh.GetPolygonCount();
				FBX_ASSERT(lMaterialIndice->GetCount() == lPolygonCount);

				if (lMaterialIndice->GetCount() == lPolygonCount)
				{
					// Count the faces of each material
					for (int lPolygonIndex = 0; lPolygonIndex < lPolygonCount; ++lPolygonIndex)
					{
						const int lMaterialIndex = lMaterialIndice->GetAt(lPolygonIndex);
						if (entry.subMeshes.size() < lMaterialIndex + 1)
						{
							entry.subMeshes.resize(lMaterialIndex + 1);
						}
						entry.subMeshes[lMaterialIndex].triangleCount++;
					}
					
					// Record the offset (how many vertex)
					const int lMaterialCount = entry.subMeshes.size();
					int lOffset = 0;
					for (int lIndex = 0; lIndex < lMaterialCount; ++lIndex)
					{
						entry.subMeshes[lIndex].indexOffset = lOffset;
						lOffset += entry.subMeshes[lIndex].triangleCount * 3;
					}
					FBX_ASSERT(lOffset == lPolygonCount * 3);
				}
			}
			else
			{
				WLOG("Mesh material not assigned by polygon!");
				entry.subMeshes.push_back(SubMeshEntry{});
			}
		}
		else
		{
			WLOG("Mesh has no material assigned to it");
			entry.subMeshes.push_back(SubMeshEntry{});
		}

		// Setup vertices and indices
		GenerateMeshEntry(entry, geometry);

		// Save mesh entry
		m_meshEntries.emplace_back(std::move(entry));

		return true;
	}

	bool FbxImport::InitializeUvSets(FbxMesh& mesh, MeshGeometry& geometry)
	{
		int32 numUvSets = 0;

        for(int32 i = 0; i < mesh.GetElementUVCount(); ++i )
        {
            const FbxGeometryElementUV* element = mesh.GetElementUV(i);
			if (element->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
			{
				++numUvSets;
			}
        }

		ILOG("\tNumber of UV Sets: " << numUvSets);
		geometry.uvSets.resize(numUvSets);
		
		return true;
	}

	bool FbxImport::LoadMeshVertexPositions(FbxNode& node, const FbxMesh& mesh, MeshGeometry& geometry)
	{
		// Global transform used to transform vertices into the expected orientation
		const auto& transform = node.EvaluateGlobalTransform();

		const auto* vertices = mesh.GetControlPoints();
		const auto vertexCount = mesh.GetControlPointsCount();

		ILOG("\tPosition count: " << vertexCount);
		geometry.positions.resize(vertexCount, Vector3::Zero);

		// Load positions
		for (size_t i = 0; i < vertexCount; ++i)
		{
			const auto transformedPosition = transform.MultT(vertices[i]);
			geometry.positions[i] = Vector3(transformedPosition[0], transformedPosition[1], transformedPosition[2]);
		}

		return true;
	}

	bool FbxImport::ImportFromFile(const Path& filename, const Path& currentAssetPath)
	{
		if (!LoadScene(filename.string()))
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
