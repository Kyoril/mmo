// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "perf_capture.h"

#include "event_loop.h"
#include "console/console.h"
#include "console/console_var.h"

#include "base/clock.h"
#include "base/profiler.h"
#include "base/signal.h"
#include "graphics/graphics_device.h"
#include "graphics/render_window.h"
#include "log/default_log_levels.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mmo
{
	namespace
	{
		using json = nlohmann::json;

		constexpr const char* GpuFrameCounter = "GPU frame (ms)";
		constexpr const char* BatchCounter = "Batches";

		/// Running sum of one profiler metric or counter over the recorded frames.
		struct MetricAccumulation
		{
			double sum = 0.0;
			double max = 0.0;
			uint64 calls = 0;
			uint64 frames = 0;

			void Add(const double value, const uint64 callCount = 0)
			{
				sum += value;
				max = std::max(max, value);
				calls += callCount;
				++frames;
			}

			[[nodiscard]] double Average() const
			{
				return frames ? sum / static_cast<double>(frames) : 0.0;
			}
		};

		/// Everything recorded about a set of frames.
		struct FrameAccumulation
		{
			std::vector<double> frameMs;
			std::vector<double> cpuMs;
			std::vector<double> gpuFrameMs;
			std::vector<double> batches;
			std::map<std::string, MetricAccumulation> metrics;
			std::map<std::string, MetricAccumulation> counters;

			/// Records the profiler's last completed frame. Returns the frame time, or 0 if none.
			double Record()
			{
				const Profiler& profiler = Profiler::GetInstance();

				const double frame = profiler.GetFrameTimeMs();
				if (frame <= 0.0)
				{
					return 0.0;
				}

				frameMs.push_back(frame);
				cpuMs.push_back(profiler.GetCpuFrameTimeMs());

				for (const auto& [name, value] : profiler.GetCounters())
				{
					counters[name].Add(value);
					if (name == GpuFrameCounter)
					{
						gpuFrameMs.push_back(value);
					}
					else if (name == BatchCounter)
					{
						batches.push_back(value);
					}
				}

				for (const auto& metric : profiler.GetMetrics())
				{
					metrics[metric.name].Add(metric.totalTimeMs, metric.callCount);
				}

				return frame;
			}
		};

		/// Per view-direction slice of a benchmark run.
		struct SegmentAccumulation
		{
			std::vector<double> frameMs;
			MetricAccumulation gpuFrame;
			std::map<std::string, MetricAccumulation> gpuPasses;
		};

		/// One configuration of an interleaved comparison (see ConsoleCommand_Benchmark).
		struct Variant
		{
			std::string name;
			std::vector<std::pair<std::string, std::string>> assignments;
			FrameAccumulation data;
		};

		enum class BenchmarkPhase
		{
			Idle,
			WaitingForWorld,
			Warmup,
			Recording
		};

		/// Settings and accumulated data of the benchmark in progress.
		struct BenchmarkState
		{
			BenchmarkPhase phase = BenchmarkPhase::Idle;

			double durationSeconds = 20.0;
			double warmupSeconds = 5.0;
			double timeoutSeconds = 240.0;
			float orbitDegrees = 360.0f;
			uint32 segmentCount = 8;
			std::string label;
			std::string outputPath = "Logs/benchmark.json";
			bool quitWhenDone = false;
			bool profilerWasEnabled = false;

			GameTime phaseStart = 0;
			GameTime armedAt = 0;

			FrameAccumulation total;
			std::vector<SegmentAccumulation> segments;

			// --- Interleaved variant comparison ---
			std::vector<Variant> variants;
			std::vector<std::pair<std::string, std::string>> restoreValues;
			double dwellSeconds = 2.0;
			double settleSeconds = 0.75;
			uint32 step = 0;
			GameTime stepStart = 0;
			bool stepApplied = false;
		};

		PerfCaptureWorldHooks s_hooks;
		BenchmarkState s_benchmark;
		scoped_connection s_idleConnection;

		/// A console command waiting for the world (see ConsoleCommand_InWorld).
		struct PendingWorldCommand
		{
			double delaySeconds = 0.0;
			std::string command;
		};

		std::vector<PendingWorldCommand> s_pendingWorldCommands;
		GameTime s_worldReadySince = 0;

		/// Returns the value at the given percentile (0..100) of an unsorted sample list.
		double Percentile(std::vector<double> values, const double percentile)
		{
			if (values.empty())
			{
				return 0.0;
			}

			std::sort(values.begin(), values.end());
			const double rank = (percentile / 100.0) * static_cast<double>(values.size() - 1);
			const size_t lower = static_cast<size_t>(rank);
			const size_t upper = std::min(lower + 1, values.size() - 1);
			const double fraction = rank - static_cast<double>(lower);
			return values[lower] + (values[upper] - values[lower]) * fraction;
		}

		double Mean(const std::vector<double>& values)
		{
			if (values.empty())
			{
				return 0.0;
			}

			double sum = 0.0;
			for (const double v : values)
			{
				sum += v;
			}
			return sum / static_cast<double>(values.size());
		}

		/// Summarizes a sample list as mean, percentiles and extremes.
		json Summarize(const std::vector<double>& values)
		{
			if (values.empty())
			{
				return json::object();
			}

			return {
				{ "mean", Mean(values) },
				{ "p50", Percentile(values, 50.0) },
				{ "p95", Percentile(values, 95.0) },
				{ "p99", Percentile(values, 99.0) },
				{ "min", *std::min_element(values.begin(), values.end()) },
				{ "max", *std::max_element(values.begin(), values.end()) },
				{ "samples", values.size() }
			};
		}

		/// Frame statistics, metrics sorted by cost and counters of an accumulation.
		json Describe(const FrameAccumulation& data)
		{
			json document;

			const double meanFrame = Mean(data.frameMs);
			document["summary"] = {
				{ "frames", data.frameMs.size() },
				{ "avgFps", meanFrame > 0.0 ? 1000.0 / meanFrame : 0.0 },
				{ "onePercentLowFps", data.frameMs.empty() ? 0.0 : 1000.0 / Percentile(data.frameMs, 99.0) },
				{ "frameMs", Summarize(data.frameMs) },
				{ "cpuMs", Summarize(data.cpuMs) },
				{ "gpuFrameMs", Summarize(data.gpuFrameMs) },
				{ "batches", Summarize(data.batches) }
			};

			// Sorted by average cost so the expensive items lead.
			std::vector<std::pair<std::string, MetricAccumulation>> sorted(data.metrics.begin(), data.metrics.end());
			std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b)
			{
				return a.second.Average() > b.second.Average();
			});

			json metrics = json::array();
			for (const auto& [name, accumulation] : sorted)
			{
				metrics.push_back({
					{ "name", name },
					{ "avgMs", accumulation.Average() },
					{ "maxMs", accumulation.max },
					{ "callsPerFrame", static_cast<double>(accumulation.calls) / static_cast<double>(std::max<uint64>(1, accumulation.frames)) },
					{ "framesSeen", accumulation.frames }
				});
			}
			document["metrics"] = std::move(metrics);

			// Per-frame counters (draws and triangles per pass, ...) as averages.
			json counters = json::object();
			for (const auto& [name, accumulation] : data.counters)
			{
				counters[name] = accumulation.Average();
			}
			document["counters"] = std::move(counters);

			return document;
		}

		std::string CurrentTimestamp()
		{
			const std::time_t now = std::time(nullptr);
			std::tm local{};
#ifdef _WIN32
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			std::ostringstream strm;
			strm << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
			return strm.str();
		}

		/// Describes the machine, build, window and every setting, so a result file explains itself.
		json DescribeEnvironment()
		{
			json environment;
			environment["timestamp"] = CurrentTimestamp();
#ifdef _DEBUG
			environment["build"] = "Debug";
#else
			environment["build"] = "Release";
#endif

			if (GraphicsDevice::HasInstance())
			{
				auto& gx = GraphicsDevice::Get();
				environment["adapter"] = gx.GetAdapterDescription();
				if (const auto window = gx.GetAutoCreatedWindow())
				{
					environment["width"] = window->GetWidth();
					environment["height"] = window->GetHeight();
				}
			}

			if (s_hooks.getLocation)
			{
				uint32 mapId = 0;
				Vector3 position;
				if (s_hooks.getLocation(mapId, position))
				{
					environment["map"] = mapId;
					environment["position"] = { position.x, position.y, position.z };
				}
			}

			json cvars = json::object();
			ConsoleVarMgr::ForEachConsoleVar([&cvars](const ConsoleVar& var)
			{
				cvars[var.GetName()] = var.GetStringValue();
			});
			environment["cvars"] = std::move(cvars);

			return environment;
		}

		bool WriteJson(const std::string& path, const json& document)
		{
			std::error_code error;
			const std::filesystem::path filePath(path);
			if (filePath.has_parent_path())
			{
				std::filesystem::create_directories(filePath.parent_path(), error);
			}

			std::ofstream file(filePath, std::ios::out | std::ios::trunc);
			if (!file)
			{
				ELOG("Unable to write performance data to " << path);
				return false;
			}

			file << document.dump(2) << "\n";
			ILOG("Performance data written to " << std::filesystem::absolute(filePath, error).string());
			return true;
		}

		/// Splits "key=value key2=value2" into a map. Keys are lowercased.
		std::map<std::string, std::string> ParseArguments(const std::string& args)
		{
			std::map<std::string, std::string> result;
			std::istringstream strm(args);
			std::string token;
			while (strm >> token)
			{
				const auto separator = token.find('=');
				std::string key = separator == std::string::npos ? token : token.substr(0, separator);
				std::transform(key.begin(), key.end(), key.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				result[key] = separator == std::string::npos ? std::string("1") : token.substr(separator + 1);
			}
			return result;
		}

		/// Parses "cvar:value;cvar2:value2" into assignments. Returns false on malformed input.
		bool ParseAssignments(const std::string& text, std::vector<std::pair<std::string, std::string>>& out)
		{
			std::istringstream strm(text);
			std::string item;
			while (std::getline(strm, item, ';'))
			{
				if (item.empty())
				{
					continue;
				}

				const auto separator = item.find(':');
				if (separator == std::string::npos || separator == 0)
				{
					return false;
				}

				out.emplace_back(item.substr(0, separator), item.substr(separator + 1));
			}

			return true;
		}

		void ConsoleCommand_PerfDump(const std::string&, const std::string& args)
		{
			auto& profiler = Profiler::GetInstance();
			if (!profiler.IsEnabled())
			{
				WLOG("The profiler is off. Run \"set perf 1\" (or benchmark) and wait a moment before dumping.");
				return;
			}

			json document;
			document["type"] = "snapshot";
			document["environment"] = DescribeEnvironment();
			document["frame"] = {
				{ "avgFrameMs", profiler.GetAverageFrameTimeMs() },
				{ "avgFps", profiler.GetAverageFPS() },
				{ "lastFrameMs", profiler.GetFrameTimeMs() },
				{ "lastCpuMs", profiler.GetCpuFrameTimeMs() }
			};

			json counters = json::object();
			for (const auto& [name, value] : profiler.GetCounters())
			{
				counters[name] = value;
			}
			document["counters"] = std::move(counters);

			json metrics = json::array();
			for (const auto& metric : profiler.GetMetrics())
			{
				metrics.push_back({
					{ "name", metric.name },
					{ "avgMs", metric.averageTimeMs },
					{ "lastMs", metric.totalTimeMs },
					{ "calls", metric.callCount },
					{ "thread", metric.threadName }
				});
			}
			document["metrics"] = std::move(metrics);

			WriteJson(args.empty() ? std::string("Logs/perf_snapshot.json") : args, document);
		}

		void ApplyAssignments(const std::vector<std::pair<std::string, std::string>>& assignments)
		{
			for (const auto& [name, value] : assignments)
			{
				// An empty value has to be quoted, or "set" sees a missing argument.
				Console::ExecuteCommand("set " + name + " " + (value.empty() ? std::string("\"\"") : value));
			}
		}

		void FinishBenchmark(const bool succeeded, const std::string& error)
		{
			const bool comparison = !s_benchmark.variants.empty();

			json document;
			document["type"] = comparison ? "comparison" : "benchmark";
			document["label"] = s_benchmark.label;
			document["succeeded"] = succeeded;
			if (!error.empty())
			{
				document["error"] = error;
			}

			document["settings"] = {
				{ "durationSeconds", s_benchmark.durationSeconds },
				{ "warmupSeconds", s_benchmark.warmupSeconds },
				{ "orbitDegrees", s_benchmark.orbitDegrees },
				{ "segments", s_benchmark.segmentCount },
				{ "dwellSeconds", s_benchmark.dwellSeconds },
				{ "settleSeconds", s_benchmark.settleSeconds }
			};

			if (comparison)
			{
				// Leave the client as it was before the comparison touched any setting.
				ApplyAssignments(s_benchmark.restoreValues);
			}

			document["environment"] = DescribeEnvironment();

			if (succeeded && !comparison)
			{
				json description = Describe(s_benchmark.total);
				for (auto& [key, value] : description.items())
				{
					document[key] = value;
				}

				json segments = json::array();
				for (size_t i = 0; i < s_benchmark.segments.size(); ++i)
				{
					const SegmentAccumulation& segment = s_benchmark.segments[i];
					const double meanSegment = Mean(segment.frameMs);

					json passes = json::object();
					for (const auto& [name, accumulation] : segment.gpuPasses)
					{
						passes[name] = accumulation.Average();
					}

					segments.push_back({
						{ "index", i },
						{ "yawDegrees", s_benchmark.orbitDegrees * (static_cast<float>(i) + 0.5f) / static_cast<float>(s_benchmark.segments.size()) },
						{ "avgFps", meanSegment > 0.0 ? 1000.0 / meanSegment : 0.0 },
						{ "frameMs", meanSegment },
						{ "gpuFrameMs", segment.gpuFrame.Average() },
						{ "gpuPasses", std::move(passes) }
					});
				}
				document["segments"] = std::move(segments);

				const double meanFrame = Mean(s_benchmark.total.frameMs);
				ILOG("Benchmark finished: " << std::fixed << std::setprecision(1)
					<< (meanFrame > 0.0 ? 1000.0 / meanFrame : 0.0) << " fps average over " << s_benchmark.total.frameMs.size()
					<< " frames, GPU frame " << Mean(s_benchmark.total.gpuFrameMs) << " ms");
			}
			else if (succeeded)
			{
				json variants = json::array();
				for (const Variant& variant : s_benchmark.variants)
				{
					json entry = Describe(variant.data);
					entry["name"] = variant.name;

					json assignments = json::object();
					for (const auto& [name, value] : variant.assignments)
					{
						assignments[name] = value;
					}
					entry["assignments"] = std::move(assignments);

					variants.push_back(std::move(entry));

					ILOG("Variant " << variant.name << ": " << std::fixed << std::setprecision(2)
						<< Mean(variant.data.frameMs) << " ms frame, " << Mean(variant.data.gpuFrameMs) << " ms GPU frame");
				}
				document["variants"] = std::move(variants);
			}
			else
			{
				ELOG("Benchmark failed: " << error);
			}

			WriteJson(s_benchmark.outputPath, document);

			s_benchmark.phase = BenchmarkPhase::Idle;
			Profiler::GetInstance().SetEnabled(s_benchmark.profilerWasEnabled);

			if (s_benchmark.quitWhenDone)
			{
				EventLoop::Terminate(succeeded ? 0 : 2);
			}
		}

		void RecordFrame(const float deltaSeconds, const double recordingSeconds)
		{
			const double frameMs = s_benchmark.total.Record();
			if (frameMs <= 0.0)
			{
				return;
			}

			const double progress = std::clamp(recordingSeconds / s_benchmark.durationSeconds, 0.0, 0.999999);
			SegmentAccumulation& segment = s_benchmark.segments[static_cast<size_t>(progress * static_cast<double>(s_benchmark.segments.size()))];
			segment.frameMs.push_back(frameMs);

			const Profiler& profiler = Profiler::GetInstance();
			if (const auto it = profiler.GetCounters().find(GpuFrameCounter); it != profiler.GetCounters().end())
			{
				segment.gpuFrame.Add(it->second);
			}

			for (const auto& metric : profiler.GetMetrics())
			{
				if (metric.name.compare(0, 5, "GPU: ") == 0)
				{
					segment.gpuPasses[metric.name].Add(metric.totalTimeMs);
				}
			}

			if (s_hooks.rotateCamera && s_benchmark.orbitDegrees != 0.0f)
			{
				s_hooks.rotateCamera(static_cast<float>(s_benchmark.orbitDegrees * deltaSeconds / s_benchmark.durationSeconds));
			}
		}

		/// Advances the interleaved comparison: every view measures every variant, alternating the
		/// order per view (ABBA) so slow drift (thermals, streaming) affects all variants alike.
		void UpdateComparison(const GameTime timestamp)
		{
			const uint32 variantCount = static_cast<uint32>(s_benchmark.variants.size());
			const uint32 totalSteps = variantCount * s_benchmark.segmentCount;
			if (s_benchmark.step >= totalSteps)
			{
				FinishBenchmark(true, std::string());
				return;
			}

			const uint32 view = s_benchmark.step / variantCount;
			const uint32 slot = s_benchmark.step % variantCount;
			const uint32 variantIndex = (view % 2 == 0) ? slot : (variantCount - 1 - slot);
			Variant& variant = s_benchmark.variants[variantIndex];

			if (!s_benchmark.stepApplied)
			{
				if (slot == 0 && view > 0 && s_hooks.rotateCamera)
				{
					s_hooks.rotateCamera(s_benchmark.orbitDegrees / static_cast<float>(s_benchmark.segmentCount));
				}

				ApplyAssignments(variant.assignments);
				s_benchmark.stepStart = timestamp;
				s_benchmark.stepApplied = true;
				return;
			}

			const double stepSeconds = static_cast<double>(timestamp - s_benchmark.stepStart) / 1000.0;
			if (stepSeconds < s_benchmark.settleSeconds)
			{
				return;
			}

			if (stepSeconds < s_benchmark.settleSeconds + s_benchmark.dwellSeconds)
			{
				variant.data.Record();
				return;
			}

			++s_benchmark.step;
			s_benchmark.stepApplied = false;
		}

		void RunPendingWorldCommands(const GameTime timestamp)
		{
			if (!s_hooks.isWorldReady || !s_hooks.isWorldReady())
			{
				s_worldReadySince = 0;
				return;
			}

			if (s_worldReadySince == 0)
			{
				s_worldReadySince = timestamp;
			}

			const double readySeconds = static_cast<double>(timestamp - s_worldReadySince) / 1000.0;
			for (auto it = s_pendingWorldCommands.begin(); it != s_pendingWorldCommands.end();)
			{
				if (readySeconds < it->delaySeconds)
				{
					++it;
					continue;
				}

				// Copy first: the command may queue further commands.
				const std::string command = it->command;
				it = s_pendingWorldCommands.erase(it);
				Console::ExecuteCommand(command);
				return;
			}
		}

		/// inworld [delaySeconds] <command>: runs a console command once the world is loaded.
		void ConsoleCommand_InWorld(const std::string&, const std::string& args)
		{
			PendingWorldCommand pending;
			std::istringstream strm(args);
			std::string first;
			strm >> first;

			char* end = nullptr;
			const double delay = std::strtod(first.c_str(), &end);
			if (end != first.c_str() && *end == '\0')
			{
				pending.delaySeconds = delay;
				std::getline(strm >> std::ws, pending.command);
			}
			else
			{
				pending.command = args;
			}

			if (pending.command.empty())
			{
				ELOG("Usage: inworld [delaySeconds] <command>");
				return;
			}

			s_pendingWorldCommands.push_back(std::move(pending));
		}

		void OnIdle(const float deltaSeconds, const GameTime timestamp)
		{
			if (!s_pendingWorldCommands.empty())
			{
				RunPendingWorldCommands(timestamp);
			}

			if (s_benchmark.phase == BenchmarkPhase::Idle)
			{
				return;
			}

			const double phaseSeconds = static_cast<double>(timestamp - s_benchmark.phaseStart) / 1000.0;

			switch (s_benchmark.phase)
			{
			case BenchmarkPhase::WaitingForWorld:
				if (s_hooks.isWorldReady && s_hooks.isWorldReady())
				{
					ILOG("Benchmark: world ready, warming up for " << s_benchmark.warmupSeconds << " s");
					s_benchmark.phase = BenchmarkPhase::Warmup;
					s_benchmark.phaseStart = timestamp;
				}
				else if (static_cast<double>(timestamp - s_benchmark.armedAt) / 1000.0 > s_benchmark.timeoutSeconds)
				{
					FinishBenchmark(false, "Timed out waiting for the world to load");
				}
				break;

			case BenchmarkPhase::Warmup:
				if (!s_hooks.isWorldReady || !s_hooks.isWorldReady())
				{
					FinishBenchmark(false, "Left the world during warmup");
				}
				else if (phaseSeconds >= s_benchmark.warmupSeconds)
				{
					ILOG("Benchmark: recording");
					s_benchmark.total = FrameAccumulation{};
					s_benchmark.segments.assign(std::max<uint32>(1, s_benchmark.segmentCount), SegmentAccumulation{});
					s_benchmark.phase = BenchmarkPhase::Recording;
					s_benchmark.phaseStart = timestamp;
				}
				break;

			case BenchmarkPhase::Recording:
				if (!s_hooks.isWorldReady || !s_hooks.isWorldReady())
				{
					FinishBenchmark(false, "Left the world while recording");
				}
				else if (!s_benchmark.variants.empty())
				{
					UpdateComparison(timestamp);
				}
				else if (phaseSeconds >= s_benchmark.durationSeconds)
				{
					FinishBenchmark(true, std::string());
				}
				else
				{
					RecordFrame(deltaSeconds, phaseSeconds);
				}
				break;

			default:
				break;
			}
		}

		void ConsoleCommand_Benchmark(const std::string&, const std::string& args)
		{
			if (s_benchmark.phase != BenchmarkPhase::Idle)
			{
				WLOG("A benchmark is already running.");
				return;
			}

			const auto arguments = ParseArguments(args);
			const auto number = [&arguments](const char* key, const double fallback)
			{
				const auto it = arguments.find(key);
				return it == arguments.end() ? fallback : std::atof(it->second.c_str());
			};
			const auto text = [&arguments](const std::string& key, const std::string& fallback)
			{
				const auto it = arguments.find(key);
				return it == arguments.end() ? fallback : it->second;
			};

			BenchmarkState state;
			state.durationSeconds = std::max(1.0, number("duration", 20.0));
			state.warmupSeconds = std::max(0.0, number("warmup", 5.0));
			state.timeoutSeconds = std::max(1.0, number("timeout", 240.0));
			state.orbitDegrees = static_cast<float>(number("orbit", 360.0));
			state.segmentCount = static_cast<uint32>(std::clamp(number("segments", 8.0), 1.0, 64.0));
			state.label = text("label", std::string());
			state.outputPath = text("out", "Logs/benchmark.json");
			state.quitWhenDone = number("quit", 0.0) != 0.0;
			state.dwellSeconds = std::max(0.25, number("dwell", 2.0));
			state.settleSeconds = std::max(0.0, number("settle", 0.75));

			// Variants v0..v9 turn the run into an interleaved comparison of setting combinations.
			for (int i = 0; i < 10; ++i)
			{
				const std::string assignmentsText = text("v" + std::to_string(i), std::string());
				if (assignmentsText.empty())
				{
					continue;
				}

				Variant variant;
				variant.name = text("n" + std::to_string(i), assignmentsText);
				if (!ParseAssignments(assignmentsText, variant.assignments))
				{
					ELOG("Invalid variant v" << i << ": expected cvar:value;cvar2:value2");
					return;
				}

				for (const auto& [name, value] : variant.assignments)
				{
					const bool known = std::any_of(state.restoreValues.begin(), state.restoreValues.end(), [&name](const auto& entry) { return entry.first == name; });
					if (!known)
					{
						const ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(name);
						state.restoreValues.emplace_back(name, var ? var->GetStringValue() : std::string());
					}
				}

				state.variants.push_back(std::move(variant));
			}

			auto& profiler = Profiler::GetInstance();
			state.profilerWasEnabled = profiler.IsEnabled();
			profiler.SetEnabled(true);

			state.phase = BenchmarkPhase::WaitingForWorld;
			state.armedAt = GetAsyncTimeMs();
			state.phaseStart = state.armedAt;
			s_benchmark = std::move(state);

			if (s_benchmark.variants.empty())
			{
				ILOG("Benchmark armed: waits for the world, warms up " << s_benchmark.warmupSeconds << " s, records "
					<< s_benchmark.durationSeconds << " s, writes " << s_benchmark.outputPath);
			}
			else
			{
				ILOG("Comparison armed: " << s_benchmark.variants.size() << " variants x " << s_benchmark.segmentCount
					<< " views, " << s_benchmark.dwellSeconds << " s each, writes " << s_benchmark.outputPath);
			}
		}
	}

	void PerfCapture::Initialize()
	{
		Console::RegisterCommand("perfdump", ConsoleCommand_PerfDump, ConsoleCommandCategory::Debug,
			"Writes the current profiler averages and settings as JSON. Usage: perfdump [file]");
		Console::RegisterCommand("benchmark", ConsoleCommand_Benchmark, ConsoleCommandCategory::Debug,
			"Records frame times in the world and writes a JSON report. Usage: benchmark [duration=20] [warmup=5] [orbit=360] [segments=8] [label=x] [out=file] [quit=0|1]. "
			"Comparison: v0=cvar:value;cvar:value v1=... [n0=name] [dwell=2] [settle=0.75] measures each variant at every view, interleaved.");

		Console::RegisterCommand("inworld", ConsoleCommand_InWorld, ConsoleCommandCategory::Debug,
			"Runs a console command once the world is loaded. Usage: inworld [delaySeconds] <command>");

		s_idleConnection = EventLoop::Idle.connect(&OnIdle);
	}

	void PerfCapture::Destroy()
	{
		s_idleConnection.disconnect();
		s_benchmark = BenchmarkState{};

		s_pendingWorldCommands.clear();
		Console::UnregisterCommand("inworld");
		Console::UnregisterCommand("benchmark");
		Console::UnregisterCommand("perfdump");
	}

	void PerfCapture::SetWorldHooks(PerfCaptureWorldHooks hooks)
	{
		s_hooks = std::move(hooks);
	}

	void PerfCapture::ClearWorldHooks()
	{
		s_hooks = PerfCaptureWorldHooks{};
	}
}
