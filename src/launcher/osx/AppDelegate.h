//
//  CAppDelegate.h
//  Sample
//
//  Created by Robin Klimonow on 29.08.13.
//  Copyright (c) 2013 Homeworks Europe. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface CAppDelegate : NSObject <NSApplicationDelegate>


- (IBAction)closeClicked:(id)sender;
- (IBAction)playClicked:(id)sender;


@property (assign) IBOutlet NSWindow *window;
@property (assign) IBOutlet NSTextField *statusLabel;
@property (assign) IBOutlet NSProgressIndicator *progressView;
@property (assign) IBOutlet NSButton *playButton;
@property (assign) IBOutlet NSTextField *currentFileLabel;
@property (assign) IBOutlet NSTextField *totalProgressLabel;


@end
