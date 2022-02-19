
// Cocoa framework header files
#import <Cocoa/Cocoa.h>

// This is the Mac OS X specific entry point of the application
int main_osx(int argc, char* argv[])
{
	@autoreleasepool {
		return NSApplicationMain(argc, (const char **)argv);
	}
}
