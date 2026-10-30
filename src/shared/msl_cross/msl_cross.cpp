// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "msl_cross.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <spirv_cross/spirv_msl.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <sstream>

namespace mmo::msl_cross
{
	namespace
	{
		// glslang moves every resource class into its own binding range, so a register keeps its
		// number and the class can still be told apart after compilation.
		constexpr int UboShift = 0;
		constexpr int TextureShift = 100;
		constexpr int SamplerShift = 200;
		constexpr int ImageShift = 300;
		constexpr int SsboShift = 400;
		constexpr int UavShift = 500;

		uint32 RegisterOf(const uint32 binding)
		{
			return binding % 100;
		}

		/// Splits a semantic like "TEXCOORD12" into its upper case name and index.
		void SplitSemantic(const std::string& semantic, std::string& name, uint32& index)
		{
			size_t digitStart = semantic.size();
			while (digitStart > 0 && std::isdigit(static_cast<unsigned char>(semantic[digitStart - 1])))
			{
				--digitStart;
			}

			name = semantic.substr(0, digitStart);
			std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
			index = digitStart < semantic.size() ? static_cast<uint32>(std::stoul(semantic.substr(digitStart))) : 0;
		}

		/// Location of a value passed from the vertex to the pixel stage, derived from its semantic
		/// only, so that a pixel shader matches every vertex shader that writes the same semantics.
		uint32 InterstageLocation(const std::string& semantic, uint32& nextFallback)
		{
			std::string name;
			uint32 index = 0;
			SplitSemantic(semantic, name, index);

			if (name == "TEXCOORD" && index < 16)
			{
				return index;
			}
			if (name == "COLOR" && index < 4)
			{
				return 16 + index;
			}
			if (name == "NORMAL" && index < 2)
			{
				return 20 + index;
			}
			if (name == "TANGENT" && index == 0)
			{
				return 22;
			}
			if (name == "BINORMAL" && index == 0)
			{
				return 23;
			}
			if (name == "POSITION" && index < 2)
			{
				return 24 + index;
			}

			return nextFallback++;
		}

		std::string NormalizedSemantic(const std::string& semantic)
		{
			std::string name;
			uint32 index = 0;
			SplitSemantic(semantic, name, index);
			return name + std::to_string(index);
		}

		class Includer final : public glslang::TShader::Includer
		{
		public:
			explicit Includer(const IncludeResolver& resolver)
				: m_resolver(resolver)
			{
			}

			IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t) override
			{
				return Resolve(headerName, includerName);
			}

			IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t) override
			{
				return Resolve(headerName, includerName);
			}

			void releaseInclude(IncludeResult* result) override
			{
				if (result)
				{
					delete static_cast<std::string*>(result->userData);
					delete result;
				}
			}

		private:
			IncludeResult* Resolve(const char* headerName, const char* includerName)
			{
				if (!m_resolver)
				{
					return nullptr;
				}

				std::optional<std::string> content = m_resolver(headerName, includerName ? includerName : "");
				if (!content)
				{
					return nullptr;
				}

				auto* data = new std::string(std::move(*content));
				return new IncludeResult(headerName, data->c_str(), data->size(), data);
			}

		private:
			const IncludeResolver& m_resolver;
		};

		EShLanguage ToGlslangStage(const metal_shader_blob::Stage stage)
		{
			switch (stage)
			{
			case metal_shader_blob::Stage::Pixel:
				return EShLangFragment;
			case metal_shader_blob::Stage::Compute:
				return EShLangCompute;
			default:
				return EShLangVertex;
			}
		}

		void EnsureGlslangInitialized()
		{
			static std::once_flag s_initFlag;
			std::call_once(s_initFlag, []()
			{
				glslang::InitializeProcess();
			});
		}

		bool CompileToSpirv(const CompileInput& input, std::vector<uint32>& spirv, std::string& log)
		{
			EnsureGlslangInitialized();

			const EShLanguage stage = ToGlslangStage(input.stage);
			glslang::TShader shader(stage);

			const char* sourceText = input.source.c_str();
			const int sourceLength = static_cast<int>(input.source.size());
			const char* sourceName = input.sourceName.c_str();
			shader.setStringsWithLengthsAndNames(&sourceText, &sourceLength, &sourceName, 1);

			std::string preamble;
			for (const auto& define : input.defines)
			{
				const size_t equals = define.find('=');
				if (equals == std::string::npos)
				{
					preamble += "#define " + define + "\n";
				}
				else
				{
					preamble += "#define " + define.substr(0, equals) + " " + define.substr(equals + 1) + "\n";
				}
			}
			shader.setPreamble(preamble.c_str());

			shader.setEntryPoint(input.entryPoint.c_str());
			shader.setSourceEntryPoint(input.entryPoint.c_str());
			shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
			shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
			shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
			shader.setEnvTargetHlslFunctionality1();
			shader.setHlslIoMapping(true);
			shader.setAutoMapLocations(true);
			shader.setShiftBinding(glslang::EResUbo, UboShift);
			shader.setShiftBinding(glslang::EResTexture, TextureShift);
			shader.setShiftBinding(glslang::EResSampler, SamplerShift);
			shader.setShiftBinding(glslang::EResImage, ImageShift);
			shader.setShiftBinding(glslang::EResSsbo, SsboShift);
			shader.setShiftBinding(glslang::EResUav, UavShift);

			const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl | EShMsgHlslOffsets | EShMsgHlslLegalization);

			Includer includer(input.includeResolver);
			if (!shader.parse(GetDefaultResources(), 100, false, messages, includer))
			{
				log = shader.getInfoLog();
				return false;
			}

			glslang::TProgram program;
			program.addShader(&shader);
			if (!program.link(messages) || !program.mapIO())
			{
				log = program.getInfoLog();
				return false;
			}

			glslang::SpvOptions options;
			options.disableOptimizer = false;
			options.optimizeSize = false;
			options.validate = false;
			spv::SpvBuildLogger logger;
			glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &logger, &options);

			log = logger.getAllMessages();
			return !spirv.empty();
		}

		void AssignBinding(spirv_cross::CompilerMSL& compiler, const spirv_cross::Resource& resource, const uint32 index)
		{
			compiler.set_decoration(resource.id, spv::DecorationBinding, index);
			compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 0);
		}

		bool TranslateToMsl(const CompileInput& input, std::vector<uint32> spirv, CompileOutput& output)
		{
			try
			{
				spirv_cross::CompilerMSL compiler(std::move(spirv));

				spirv_cross::CompilerMSL::Options mslOptions = compiler.get_msl_options();
				mslOptions.platform = spirv_cross::CompilerMSL::Options::macOS;
				mslOptions.set_msl_version(2, 3);
				mslOptions.enable_decoration_binding = true;
				mslOptions.pad_fragment_output_components = true;
				compiler.set_msl_options(mslOptions);

				const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

				for (const auto& resource : resources.uniform_buffers)
				{
					AssignBinding(compiler, resource, RegisterOf(compiler.get_decoration(resource.id, spv::DecorationBinding)));
				}

				for (const auto& resource : resources.storage_buffers)
				{
					const uint32 reg = RegisterOf(compiler.get_decoration(resource.id, spv::DecorationBinding));
					const bool readOnly = compiler.get_buffer_block_flags(resource.id).get(spv::DecorationNonWritable);
					AssignBinding(compiler, resource, (readOnly ? metal_bindings::StructuredBufferBase : metal_bindings::RWStructuredBufferBase) + reg);
				}

				for (const auto& resource : resources.separate_images)
				{
					AssignBinding(compiler, resource, RegisterOf(compiler.get_decoration(resource.id, spv::DecorationBinding)));
				}

				for (const auto& resource : resources.sampled_images)
				{
					AssignBinding(compiler, resource, RegisterOf(compiler.get_decoration(resource.id, spv::DecorationBinding)));
				}

				for (const auto& resource : resources.storage_images)
				{
					AssignBinding(compiler, resource, metal_bindings::RWTextureBase + RegisterOf(compiler.get_decoration(resource.id, spv::DecorationBinding)));
				}

				for (const auto& resource : resources.separate_samplers)
				{
					AssignBinding(compiler, resource, RegisterOf(compiler.get_decoration(resource.id, spv::DecorationBinding)));
				}

				uint32 nextFallbackLocation = 26;
				std::vector<std::pair<std::string, uint32>> matrixInputs;
				if (input.stage == metal_shader_blob::Stage::Vertex)
				{
					// Vertex attributes are matched to vertex declarations by name at pipeline creation.
					for (const auto& resource : resources.stage_inputs)
					{
						const std::string& semantic = compiler.get_decoration_string(resource.id, spv::DecorationUserSemantic);
						if (!semantic.empty())
						{
							compiler.set_name(resource.id, NormalizedSemantic(semantic));

							const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
							if (type.columns > 1 && type.columns == type.vecsize)
							{
								matrixInputs.emplace_back(NormalizedSemantic(semantic), type.columns);
							}
						}
					}

					for (const auto& resource : resources.stage_outputs)
					{
						const std::string& semantic = compiler.get_decoration_string(resource.id, spv::DecorationUserSemantic);
						if (!semantic.empty())
						{
							compiler.set_decoration(resource.id, spv::DecorationLocation, InterstageLocation(semantic, nextFallbackLocation));
						}
					}
				}
				else if (input.stage == metal_shader_blob::Stage::Pixel)
				{
					for (const auto& resource : resources.stage_inputs)
					{
						const std::string& semantic = compiler.get_decoration_string(resource.id, spv::DecorationUserSemantic);
						if (!semantic.empty())
						{
							compiler.set_decoration(resource.id, spv::DecorationLocation, InterstageLocation(semantic, nextFallbackLocation));
						}
					}
				}

				output.source = compiler.compile();

				// A matrix vertex input takes one register per column in D3D (the engine declares them
				// column_major), but glslang ignores the majorness of inputs and SPIRV-Cross assembles
				// the matrix from the attributes as columns of the transposed layout. Transpose it back
				// right after it was assembled.
				for (const auto& [name, columns] : matrixInputs)
				{
					const std::string lastAssignment = name + "[" + std::to_string(columns - 1) + "] = in." + name + "_" + std::to_string(columns - 1) + ";";
					const size_t position = output.source.find(lastAssignment);
					if (position == std::string::npos)
					{
						output.log += "Unable to find the assembly of matrix input " + name + "\n";
						return false;
					}

					output.source.insert(position + lastAssignment.size(), "\n    " + name + " = transpose(" + name + ");");
				}

				if (input.stage == metal_shader_blob::Stage::Compute)
				{
					const auto& entryPoint = compiler.get_entry_point(compiler.get_entry_points_and_stages().front().name, spv::ExecutionModelGLCompute);
					output.threadgroupSize[0] = std::max(1u, entryPoint.workgroup_size.x);
					output.threadgroupSize[1] = std::max(1u, entryPoint.workgroup_size.y);
					output.threadgroupSize[2] = std::max(1u, entryPoint.workgroup_size.z);
				}
			}
			catch (const std::exception& e)
			{
				output.log += std::string("SPIRV-Cross: ") + e.what();
				return false;
			}

			return true;
		}
	}

	bool CompileHlslToMsl(const CompileInput& input, CompileOutput& output)
	{
		output = CompileOutput{};

		std::vector<uint32> spirv;
		if (!CompileToSpirv(input, spirv, output.log))
		{
			return false;
		}

		output.succeeded = TranslateToMsl(input, std::move(spirv), output);
		return output.succeeded;
	}
}
