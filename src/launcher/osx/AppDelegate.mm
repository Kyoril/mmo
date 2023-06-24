
#import "AppDelegate.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <chrono>
#include <memory>
#include <optional>
#include <iterator>
#include <any>
#include <array>
#include <set>
#include "asio.hpp"

#include "base/typedefs.h"
#include "base/macros.h"

#include "log/log.h"
#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "log/log_entry.h"

#include "updater/open_source_from_url.h"
#include "updater/update_url.h"
#include "updater/prepare_parameters.h"
#include "updater/update_parameters.h"
#include "updater/prepare_update.h"
#include "updater/update_source.h"
#include "updater/updater_progress_handler.h"
#include "updater/prepare_progress_handler.h"
#include "updater/update_application.h"

#define MMO_LAUNCHER_VERSION 3

std::uintmax_t updateSize = 0;
std::uintmax_t updated = 0;
std::uintmax_t lastUpdateStatus = 0;

size_t updatePerformanceConcurrency = 1;

namespace mmo
{
	namespace updating
	{
		struct OSXProgressHandler
		: IPrepareProgressHandler
		, IUpdaterProgressHandler
		{
			virtual void updateFile(const std::string &name, std::uintmax_t size, std::uintmax_t loaded) override
			{
				@autoreleasepool {
					
					// Increment download counter
					if (loaded >= lastUpdateStatus)
					{
						updated += loaded - lastUpdateStatus;
					}
					lastUpdateStatus = loaded;
					
					// Get app delegate object
					CAppDelegate* delegate = (CAppDelegate*)[NSApplication sharedApplication].delegate;
					
					// Update status message
					[delegate performSelectorOnMainThread:@selector(updateStatusMessage:) withObject:[NSString stringWithFormat:@"Updating file %s...", name.c_str()] waitUntilDone:YES];
					
					// Update current file info
					[delegate performSelectorOnMainThread:@selector(updateCurrentFileMessage:) withObject:[NSString stringWithFormat:@"%u / %u", loaded, size] waitUntilDone:YES];
					
					// Calculate total percentage
					double percentage = (static_cast<double>(updated) / static_cast<double>(updateSize));
					
					// Update Progress bar
					[delegate performSelectorOnMainThread:@selector(updateProgressBar:) withObject:[NSNumber numberWithDouble:percentage] waitUntilDone:YES];
					
					// Update total progress info
					[delegate performSelectorOnMainThread:@selector(updateTotalProgressMessage:) withObject:[NSString stringWithFormat:@"%u / %u (%d%%)", updated, updateSize, static_cast<int>(percentage * 100.0)] waitUntilDone:YES];
					
					// Log file process
					if (loaded >= size)
					{
						// Reset counter
						lastUpdateStatus = 0;
						NSLog(@"Successfully loaded file %s (Size: %u bytes)", name.c_str(), size);
					}
				}
			}
			
			virtual void beginCheckLocalCopy(const std::string &name) override
			{
			}
			
		private:
			
		};
	}
}


@implementation CAppDelegate

- (void)updateStatusMessage:(NSString*)message
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [_statusLabel setStringValue:message];
    });
}

- (void)updateCurrentFileMessage:(NSString*)message
{
    dispatch_async(dispatch_get_main_queue(), ^{
	[_currentFileLabel setStringValue:message];
    });
}

- (void)updateTotalProgressMessage:(NSString*)message
{
        dispatch_async(dispatch_get_main_queue(), ^{
	[_totalProgressLabel setStringValue:message];
        });
}

- (void)updateProgressBar:(NSNumber*)value
{
            dispatch_async(dispatch_get_main_queue(), ^{
	[_progressView setDoubleValue:[value doubleValue]];
            });
}

- (void)updateThread
{
	@autoreleasepool {
        NSLog(@"Starting update thread...");
        
		const std::string outputDir = "./";
		std::set<std::string> conditionsSet;
		conditionsSet.insert("OSX");
		
		// Check windows
		if (sizeof(void *) == 8)
		{
			conditionsSet.insert("X64");
		}
		else
		{
			conditionsSet.insert("X86");
		}
		
		bool doUnpackArchives = false;
		mmo::updating::OSXProgressHandler progressHandler;
		auto source = mmo::updating::openSourceFromUrl(
                                                       mmo::updating::UpdateURL("http://patch.mmo-dev.net")
														  );
		
        mmo::updating::PrepareParameters prepareParameters(
															  std::move(source),
															  conditionsSet,
															  doUnpackArchives,
															  progressHandler
															  );

		try
		{
			const auto preparedUpdate = mmo::updating::prepareUpdate(
																		outputDir,
																		prepareParameters
																		);
			
			// Get download size
			updateSize = preparedUpdate.estimates.downloadSize;
			
			mmo::updating::UpdateParameters updateParameters(
																std::move(prepareParameters.source),
																doUnpackArchives,
																progressHandler
																);
			
			{
				asio::io_service dispatcher;
				for (const auto & step : preparedUpdate.steps)
				{
					dispatcher.post(
									[&]()
									{
										std::string errorMessage;
										try
										{
											while (step.step(updateParameters)) {
												;
											}
											
											return;
										}
										catch (const std::exception &ex)
										{
											errorMessage = ex.what();
										}
										catch (...)
										{
											errorMessage = "Caught an exception not derived from std::exception";
										}
										
										dispatcher.stop();
										dispatcher.post([errorMessage]()
														{
															throw std::runtime_error(errorMessage);
														});
									});
				}
				
                std::vector<std::thread> threads;
                threads.emplace_back(std::thread([&dispatcher] { dispatcher.run(); } ));

                std::for_each(
                    threads.begin(),
                    threads.end(),
                    std::bind(&std::thread::join, std::placeholders::_1));

                dispatcher.run();
			}
			
			// Enable play button
			[self updateStatusMessage:@"Client is up-to-date!"];
            dispatch_async(dispatch_get_main_queue(), ^{
                [_playButton setEnabled:YES];
            });
		}
		catch (const std::exception &e)
		{
            NSString* errorMessage = [NSString stringWithUTF8String:e.what()];
            
            dispatch_async(dispatch_get_main_queue(), ^{
                // Log error
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = @"Could not update Game Client";
                alert.informativeText = errorMessage;
                alert.alertStyle = NSAlertStyleCritical;
                [alert beginSheetModalForWindow:self.window modalDelegate:nil didEndSelector:nil contextInfo:nil];
            });
		}
	}
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	// Setup chdir since in os x it is necessary
	NSString* appBundlePath = [[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent];
	chdir([appBundlePath UTF8String]);
	
	// Run update thread
	NSThread* thread = [[NSThread alloc] initWithTarget:self selector:@selector(updateThread) object:nil];
	[thread start];
}

- (IBAction)closeClicked:(id)sender
{
	// Terminate application
	[[NSApplication sharedApplication] terminate:sender];
}

- (IBAction)playClicked:(id)sender
{
	
}

@end
