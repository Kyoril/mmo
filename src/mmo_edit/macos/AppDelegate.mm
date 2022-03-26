
#import "AppDelegate.h"

namespace mmo
{
    bool InitializeGlobal();

    void DestroyGlobal();
}

@implementation CAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    mmo::InitializeGlobal();
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
    mmo::DestroyGlobal();
}

@end
