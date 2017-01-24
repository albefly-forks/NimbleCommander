//
//  CreateDirectorySheetController.m
//  Directories
//
//  Created by Michael G. Kazakov on 01.03.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#include <NimbleCommander/Core/GoogleAnalytics.h>
#include <NimbleCommander/Core/Theming/CocoaAppearanceManager.h>
#include "CreateDirectorySheetController.h"

@interface CreateDirectorySheetController()

- (IBAction)OnCreate:(id)sender;
- (IBAction)OnCancel:(id)sender;
@property (strong) IBOutlet NSTextField *TextField;
@property (strong) IBOutlet NSButton *CreateButton;

@end


@implementation CreateDirectorySheetController
{
    string m_Result;
}

@synthesize result = m_Result;

- (void)windowDidLoad
{
    [super windowDidLoad];
    CocoaAppearanceManager::Instance().ManageWindowApperance(self.window);
    [self.window makeFirstResponder:self.TextField];
    GA().PostScreenView("Create Directory");
}

- (IBAction)OnCreate:(id)sender
{
    if( !self.TextField.stringValue || !self.TextField.stringValue.length )
        return;
    
    if( auto p = self.TextField.stringValue.fileSystemRepresentation )
        m_Result = p;
    
    [self endSheet:NSModalResponseOK];
}

- (IBAction)OnCancel:(id)sender
{
    [self endSheet:NSModalResponseCancel];
}

@end
