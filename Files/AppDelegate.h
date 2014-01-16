//
//  AppDelegate.h
//  Directories
//
//  Created by Michael G. Kazakov on 08.02.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <vector>

#import "ApplicationSkins.h"

using namespace std;

@class MainWindowController;



@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowRestoration>

@property (nonatomic, readonly) ApplicationSkin Skin;

+ (void)initialize;

- (IBAction)NewWindow:(id)sender;
- (void) RemoveMainWindow:(MainWindowController*) _wnd;

- (IBAction)OnMenuSendFeedback:(id)sender;

- (vector<MainWindowController*>) GetMainWindowControllers;

@end
