//
//  MainWindowController.h
//  Directories
//
//  Created by Michael G. Kazakov on 09.02.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "chained_strings.h"
#import "ApplicationSkins.h"
#import "VFS.h"

@class OperationsController;
@class MainWindowFilePanelState;
@class MainWindowTerminalState;

@interface MainWindowController : NSWindowController <NSWindowDelegate>

// Window state manipulations
- (void) ResignAsWindowState:(id)_state;

- (OperationsController*) OperationsController;

- (void)ApplySkin:(ApplicationSkin)_skin;
- (void)OnApplicationWillTerminate;

- (void)RevealEntries:(chained_strings)_entries inPath:(const char*)_path;

- (void)RequestBigFileView: (const char*)_filepath with_fs:(shared_ptr<VFSHost>) _host;

- (void)RequestTerminal:(const char*)_cwd;
- (void)RequestTerminalExecution:(const char*)_filename at:(const char*)_cwd;

- (MainWindowFilePanelState*) FilePanelState; // one and only one per window
- (MainWindowTerminalState*) TerminalState;   // zero or one per window
@end

