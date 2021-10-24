#include "update_application.h"
#include "prepared_update.h"
#include "base/create_process.h"
#include "base/clock.h"

#include <iostream>


namespace mmo::updating
{
	namespace
	{
		std::string makeRandomString()
		{
			return std::to_string(GetAsyncTimeMs());
		}
	}

	ApplicationUpdate updateApplication(
	    const std::filesystem::path &applicationPath,
	    const PreparedUpdate &preparedUpdate
	)
	{
		ApplicationUpdate update;

		for (const mmo::updating::PreparedUpdateStep & step : preparedUpdate.steps)
		{
			try
			{
				if (!std::filesystem::equivalent(applicationPath, step.destinationPath))
				{
					continue;
				}
			}
			catch (const std::filesystem::filesystem_error &ex)
			{
				std::cerr << ex.what() << '\n';
				continue;
			}

			update.perform = [applicationPath, step](
			                     const UpdateParameters & updateParameters,
			                     const char * const * argv,
			                     size_t argc) -> void
			{
				const std::filesystem::path executableCopy =
				applicationPath.string() +
				"." +
				makeRandomString();

				std::filesystem::rename(
				    applicationPath,
				    executableCopy
				);

				try
				{
					while (step.step(updateParameters)) {
						;
					}

					mmo::makeExecutable(applicationPath.string());
				}
				catch (...)
				{
					//revert the copy in case of error
					try
					{
						std::filesystem::rename(
						    executableCopy,
						    applicationPath
						);
					}
					catch (...)
					{
					}

					throw;
				}

				std::vector<std::string> arguments(argv, argv + argc);
				arguments.push_back("--remove-previous \"" + executableCopy.string() + "\"");
				mmo::createProcess(applicationPath.string(), arguments);
			};

			break;
		}

		return update;
	}
}
