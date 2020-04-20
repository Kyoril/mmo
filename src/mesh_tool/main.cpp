// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "math/vector3.h"
#include "base/pluralize.h"

#include "cxxopts/cxxopts.hpp"
#include "fbxsdk.h"

#include <fstream>
#include <memory>
#include <string>
#include <mutex>

/// String containing the version of this tool.
static const std::string VersionStr = "1.0.0";

#ifdef IOS_REF
#	undef  IOS_REF
#	define IOS_REF (*(pManager->GetIOSettings()))
#endif

namespace mmo
{
	/// Represents a single vertex of a mesh.
	struct Vertex
	{
		/// Position vector
		Vector3 position;
		/// Normal vector
		Vector3 normal;
		/// Texture coordinates
		Vector3 texCoord;
		/// Vertex color
		uint32 color;
	};

	/// Contains mesh data.
	struct MeshEntry
	{
		/// Name of the mesh.
		std::string name;
		/// Vertex data.
		std::vector<Vertex> vertices;
		/// Index data.
		std::vector<uint32> indices;
		/// Max index to determine whether we can use 16 bit index buffers.
		uint32 maxIndex;
	};
}

/// A list of mesh entries found in the fbx scene.
std::vector<mmo::MeshEntry> g_meshEntries;


namespace fbx
{
	void InitializeSdkObjects(FbxManager*& pManager, FbxScene*& pScene)
	{
		//The first thing to do is to create the FBX Manager which is the object allocator for almost all the classes in the SDK
		pManager = FbxManager::Create();
		if (!pManager)
		{
			ELOG("Error: Unable to create FBX Manager!");
			exit(1);
		}
		else ILOG("Autodesk FBX SDK version " << pManager->GetVersion());

		//Create an IOSettings object. This object holds all import/export settings.
		FbxIOSettings* ios = FbxIOSettings::Create(pManager, IOSROOT);
		pManager->SetIOSettings(ios);

		//Create an FBX scene. This object holds most objects imported/exported from/to files.
		pScene = FbxScene::Create(pManager, "My Scene");
		if (!pScene)
		{
			ELOG("Error: Unable to create FBX scene!");
			exit(1);
		}
	}

	void DestroySdkObjects(FbxManager* pManager, bool pExitStatus)
	{
		//Delete the FBX Manager. All the objects that have been allocated using the FBX Manager and that haven't been explicitly destroyed are also automatically destroyed.
		if (pManager) pManager->Destroy();
		if (pExitStatus) ILOG("Program Success!");
	}

	bool LoadScene(FbxManager* pManager, FbxDocument* pScene, const char* pFilename)
	{
		int lFileMajor, lFileMinor, lFileRevision;
		int lSDKMajor, lSDKMinor, lSDKRevision;
		//int lFileFormat = -1;
		int i, lAnimStackCount;
		bool lStatus;

		// Get the file version number generate by the FBX SDK.
		FbxManager::GetFileFormatVersion(lSDKMajor, lSDKMinor, lSDKRevision);

		// Create an importer.
		FbxImporter* lImporter = FbxImporter::Create(pManager, "");

		// Initialize the importer by providing a filename.
		const bool lImportStatus = lImporter->Initialize(pFilename, -1, pManager->GetIOSettings());
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
		lStatus = lImporter->Import(pScene);

		if (lStatus == false && lImporter->GetStatus().GetCode() == FbxStatus::ePasswordError)
		{
			ELOG("FBX file is protected by a password, won't import file!");
		}

		// Destroy the importer.
		lImporter->Destroy();

		return lStatus;
	}

	void TraverseScene(FbxScene& scene, FbxNode& node)
	{
		// Find mesh nodes
		const FbxMesh* mesh = node.GetMesh();
		if (mesh)
		{
			// Create a new mesh entry
			mmo::MeshEntry entry;
			entry.name = node.GetName();
			entry.maxIndex = 0;

			// Gets the control points of the mesh
			FbxVector4* vertices = mesh->GetControlPoints();
			int vertexCount = mesh->GetControlPointsCount();
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
						g_meshEntries.push_back(entry);
					}
				}
			}
		}

		// Recursively traverse child nodes
		const int childCount = node.GetChildCount();
		for (int i = 0; i < childCount; ++i)
		{
			TraverseScene(scene, *node.GetChild(i));
		}
	}
}

/// Procedural entry point of the application.
int main(int argc, char** argv)
{
	// Add cout to the list of log output streams
	std::mutex coutLogMutex;
	mmo::g_DefaultLog.signal().connect([&coutLogMutex](const mmo::LogEntry & entry) 
	{
		std::scoped_lock lock{ coutLogMutex };
		mmo::printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions);
	});

	// Parameters for source and target file name, eventually filled by command line options
	std::string sourceFile;
	std::string targetFile;

	// Prepare available command line options
	cxxopts::Options options("Mesh Tool " + VersionStr + ", available options");
	options.add_options()
		("help", "produce help message")
		("s,source", "set source file name", cxxopts::value<std::string>(sourceFile))
		("t,target", "set target file name", cxxopts::value<std::string>(targetFile))
		;

	// Add positional parameters
	options.parse_positional({ "source", "target" });

	// Catch exceptions from command line argument parsing. This is a huge try-block because
	// the cxxopts interface has no default constructor for parse results, thus the call to
	// parse needs to stay valid this whole block.
	try
	{
		// Parse command line arguments
		cxxopts::ParseResult result = std::move(options.parse(argc, argv));

		// Check for help output
		if (result.count("help"))
		{
			ILOG(options.help());
			return 0;
		}

		// Prepare the FBX SDK.
		FbxManager* lSdkManager = nullptr;
		FbxScene* lScene = nullptr;
		fbx::InitializeSdkObjects(lSdkManager, lScene);

		// Load the scene from the fbx file
		if (!fbx::LoadScene(lSdkManager, lScene, sourceFile.c_str()))
		{
			ELOG("Failed to load input fbx file " << sourceFile << "!");
			return 1;
		}

		// Create a converter
		FbxGeometryConverter converter(lSdkManager);
		converter.Triangulate(lScene, true);

		// Find root node
		FbxNode* rootNode = lScene->GetRootNode();
		if (rootNode == nullptr)
		{
			ELOG("Fbx file has no root node!");
			return 1;
		}

		// Traverse through all nodes
		fbx::TraverseScene(*lScene, *rootNode);

		// Check if target file has been set
		if (targetFile.empty())
		{
			// Remove extension of source file if there is any and apply the mesh extension
			const size_t extensionDot = sourceFile.rfind('.');
			if (extensionDot != sourceFile.npos)
			{
				targetFile = sourceFile.substr(0, extensionDot) + ".mesh";
			}
			else
			{
				targetFile = sourceFile + ".mesh";
			}
		}
		else
		{
			// Make sure that the file extension is *.mesh
			const size_t htexExtension = targetFile.rfind(".mesh");
			if (htexExtension == targetFile.npos)
			{
				targetFile = targetFile + ".mesh";
			}
		}

		// Notify about mesh count
		DLOG("Found " << g_meshEntries.size() << " " << mmo::Pluralize("mesh", g_meshEntries.size()) << "!");

		// Open the output file
		std::ofstream dstFile{ targetFile.c_str(), std::ios::out | std::ios::binary };
		if (!dstFile)
		{
			ELOG("Could not open target file " << targetFile);
			return 1;
		}

		// Output buffer
		std::vector<uint8> buffer;

		// Write the file
		{
			// TODO

		}

		// Delete the FBX SDK manager. All the objects that have been allocated 
		// using the FBX SDK manager and that haven't been explicitly destroyed 
		// are automatically destroyed at the same time.
		fbx::DestroySdkObjects(lSdkManager, true);
	}
	catch (const cxxopts::OptionException& e)
	{
		// Command line argument parse exception, print it and also the help string
		ELOG(e.what());
		ELOG(options.help());
		return 1;
	}

	return 0;
}
