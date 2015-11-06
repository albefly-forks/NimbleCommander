//
//  FileDeletionSheetWindowController.h
//  Directories
//
//  Created by Pavel Dogurevich on 15.04.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#include "FileDeletionOperation.h"
#include "../../ButtonWithOptions.h"

typedef void (^FileDeletionSheetCompletionHandler)(int result);

@interface FileDeletionSheetController : NSWindowController

@property (strong) IBOutlet NSTextField *Label;
@property (strong) IBOutlet ButtonWithOptions *DeleteButton;
@property (strong) IBOutlet NSMenu *DeleteButtonMenu;

@property (nonatomic) bool allowMoveToTrash;
@property (nonatomic) bool allowSecureDelete;
@property (nonatomic) FileDeletionOperationType resultType;

- (IBAction)OnDeleteAction:(id)sender;
- (IBAction)OnCancelAction:(id)sender;
- (IBAction)OnMenuItem:(NSMenuItem *)sender;

- (id)init;

- (void)ShowSheet:(NSWindow *)_window
            Files:(const vector<string>&)_files
             Type:(FileDeletionOperationType)_type
          Handler:(FileDeletionSheetCompletionHandler)_handler;

@end