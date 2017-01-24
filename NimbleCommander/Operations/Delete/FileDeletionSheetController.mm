//
//  FileDeletionSheetWindowController.m
//  Directories
//
//  Created by Pavel Dogurevich on 15.04.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#include <NimbleCommander/Core/GoogleAnalytics.h>
#include <NimbleCommander/Core/Theming/CocoaAppearanceManager.h>
#include "FileDeletionSheetController.h"

@interface FileDeletionSheetController()

@property (strong) IBOutlet NSTextField *Label;
@property (strong) IBOutlet NSButton *primaryActionButton;
@property (strong) IBOutlet NSPopUpButton *auxiliaryActionPopup;

@end

@implementation FileDeletionSheetController
{
    FileDeletionOperationType m_DefaultType;
    FileDeletionOperationType m_ResultType;

    shared_ptr<vector<VFSListingItem>> m_Items;
    bool                        m_AllowMoveToTrash;
}

@synthesize allowMoveToTrash = m_AllowMoveToTrash;
@synthesize resultType = m_ResultType;
@synthesize defaultType = m_DefaultType;

- (id)initWithItems:(const shared_ptr<vector<VFSListingItem>>&)_items
{
    self = [super init];
    if (self) {
        m_AllowMoveToTrash = true;
        m_DefaultType = FileDeletionOperationType::Delete;
        m_ResultType = FileDeletionOperationType::Delete;
        m_Items = _items;
    }
    
    return self;
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    CocoaAppearanceManager::Instance().ManageWindowApperance(self.window);
    
    if( m_DefaultType == FileDeletionOperationType::MoveToTrash ) {
        self.primaryActionButton.title = NSLocalizedString(@"Move to Trash", "Menu item title in file deletion sheet");
    
        [self.auxiliaryActionPopup addItemWithTitle:NSLocalizedString(@"Delete Permanently", "Menu item title in file deletion sheet")];
        self.auxiliaryActionPopup.lastItem.target = self;
        self.auxiliaryActionPopup.lastItem.action = @selector(onAuxActionPermDelete:);
    }
    else if( m_DefaultType == FileDeletionOperationType::Delete ) {
        self.primaryActionButton.title = NSLocalizedString(@"Delete Permanently", "Menu item title in file deletion sheet");
        
        if( m_AllowMoveToTrash ) {
            [self.auxiliaryActionPopup addItemWithTitle:NSLocalizedString(@"Move to Trash", "Menu item title in file deletion sheet")];
            self.auxiliaryActionPopup.lastItem.target = self;
            self.auxiliaryActionPopup.lastItem.action = @selector(onAuxActionTrash:);
        }
        else {
            self.auxiliaryActionPopup.enabled = false;
        }
    }
    
    [self buildTitle];
    
    GA().PostScreenView("Delete Files");
}

- (IBAction)onPrimaryAction:(id)sender
{
    m_ResultType = m_DefaultType;
    [self endSheet:NSModalResponseOK];
}

- (IBAction)onAuxActionTrash:(id)sender
{
    m_ResultType = FileDeletionOperationType::MoveToTrash;
    [self endSheet:NSModalResponseOK];
}

- (IBAction)onAuxActionPermDelete:(id)sender
{
    m_ResultType = FileDeletionOperationType::Delete;
    [self endSheet:NSModalResponseOK];
}

- (IBAction)OnCancelAction:(id)sender
{
    [self endSheet:NSModalResponseCancel];
}

- (void)moveRight:(id)sender
{
    [self.window selectNextKeyView:sender];
}

- (void)moveLeft:(id)sender
{
    [self.window selectPreviousKeyView:sender];
}

- (void) buildTitle
{
    if(m_Items->size() == 1)
        self.Label.stringValue = [NSString stringWithFormat:NSLocalizedString(@"Do you want to delete “%@”?", "Asking user to delete a file"),
                                  [NSString stringWithUTF8String:m_Items->front().Name()]];
    else
        self.Label.stringValue = [NSString stringWithFormat:NSLocalizedString(@"Do you want to delete %@ items?", "Asking user to delete multiple files"),
                                  [NSNumber numberWithUnsignedLong:m_Items->size()]];
}

@end
