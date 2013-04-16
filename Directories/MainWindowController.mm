
//
//  MainWindowController.m
//  Directories
//
//  Created by Michael G. Kazakov on 09.02.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import "MainWindowController.h"
#import "PanelController.h"
#import "AppDelegate.h"
#import "CreateDirectorySheetController.h"
#import "MassCopySheetController.h"
#import "DetailedVolumeInformationSheetController.h"
#import "FileSysEntryAttrSheetController.h"
#import "FileDeletionSheetController.h"
#import "FlexChainedStringsChunk.h"
#import "OperationsController.h"
#import "OperationsSummaryViewController.h"
#import "FileSysAttrChangeOperation.h"
#import "FileDeletionOperation.h"
#import "CreateDirectoryOperation.h"
#import "FileCopyOperation.h"
#import "MessageBox.h"
#import "KQueueDirUpdate.h"
#import "FSEventsDirUpdate.h"
#import <pwd.h>

// TODO: remove
#import "TimedDummyOperation.h"


@interface MainWindowController ()

@end

@implementation MainWindowController
{
    ActiveState m_ActiveState;                  // creates and owns

    PanelView *m_LeftPanelView;                 // creates and owns
    PanelData *m_LeftPanelData;                 // creates and owns
    PanelController *m_LeftPanelController;     // creates and owns
    
    PanelView *m_RightPanelView;                // creates and owns
    PanelData *m_RightPanelData;                // creates and owns
    PanelController *m_RightPanelController;    // creates and owns
    struct
    {
        NSLayoutConstraint *left_left;
        NSLayoutConstraint *left_bottom;
        NSLayoutConstraint *left_top;
        NSLayoutConstraint *left_right;
        NSLayoutConstraint *right_left;
        NSLayoutConstraint *right_bottom;
        NSLayoutConstraint *right_top;
        NSLayoutConstraint *right_right;
    } m_PanelConstraints;
    
    OperationsController *m_OperationsController;
    OperationsSummaryViewController *m_OpSummaryController;
}

- (id)init {
    self = [super initWithWindowNibName:@"MainWindowController"];
    
    if (self)
    {
        m_OperationsController = [[OperationsController alloc] init];
        m_OpSummaryController = [[OperationsSummaryViewController alloc] initWthController:m_OperationsController];
    }
    
    return self;
}

-(void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)windowDidLoad
{
    [super windowDidLoad];
 
    // Implement this method to handle any initialization after your window controller's window has been loaded from its nib file.
    
    [m_OpSummaryController AddViewTo:self.OpSummaryBox];

    struct passwd *pw = getpwuid(getuid());
    assert(pw);

    [self CreatePanels];
    [self CreatePanelConstraints];
    
    m_LeftPanelData = new PanelData;
    m_LeftPanelController = [PanelController new];
    [m_LeftPanelView SetPanelData:m_LeftPanelData];
    [m_LeftPanelController SetView:m_LeftPanelView];
    [m_LeftPanelController SetData:m_LeftPanelData];
    [m_LeftPanelController GoToDirectory:pw->pw_dir];

    m_RightPanelData = new PanelData;
    m_RightPanelController = [PanelController new];
    [m_RightPanelView SetPanelData:m_RightPanelData];
    [m_RightPanelController SetView:m_RightPanelView];
    [m_RightPanelController SetData:m_RightPanelData];
    [m_RightPanelController GoToDirectory:"/"];
    
    m_ActiveState = StateLeftPanel;
    [m_LeftPanelView Activate];
    
    [[self window] makeFirstResponder:self];
    [[self window] setDelegate:self];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(DidBecomeKeyWindow)
                                                 name:NSWindowDidBecomeKeyNotification
                                               object:[self window]];
    
//    [[self window] visualizeConstraints:[[[self window] contentView] constraints]];
}

- (void)CreatePanels
{
    m_LeftPanelView = [[PanelView alloc] initWithFrame:NSMakeRect(0, 200, 100, 100)];
    [m_LeftPanelView setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[[self window] contentView] addSubview:m_LeftPanelView];

    m_RightPanelView = [[PanelView alloc] initWithFrame:NSMakeRect(100, 100, 100, 100)];
    [m_RightPanelView setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[[self window] contentView] addSubview:m_RightPanelView];    
}

- (void)CreatePanelConstraints
{
    const int topgap = 45;
    [[[self window] contentView] removeConstraint:m_PanelConstraints.left_left];
    [[[self window] contentView] removeConstraint:m_PanelConstraints.left_top];
    [[[self window] contentView] removeConstraint:m_PanelConstraints.left_right];
    [[[self window] contentView] removeConstraint:m_PanelConstraints.left_bottom];
    [[[self window] contentView] removeConstraint:m_PanelConstraints.right_left];
    [[[self window] contentView] removeConstraint:m_PanelConstraints.right_top];
    [[[self window] contentView] removeConstraint:m_PanelConstraints.right_right];
    [[[self window] contentView] removeConstraint:m_PanelConstraints.right_bottom];
    
    m_PanelConstraints.left_left = [NSLayoutConstraint constraintWithItem:m_LeftPanelView
                                                                attribute:NSLayoutAttributeLeft
                                                                relatedBy:NSLayoutRelationEqual
                                                                   toItem:[[self window] contentView]
                                                                attribute:NSLayoutAttributeLeft
                                                               multiplier:1
                                                                 constant:0];
    m_PanelConstraints.left_top = [NSLayoutConstraint constraintWithItem:m_LeftPanelView
                                                                attribute:NSLayoutAttributeTop
                                                                relatedBy:NSLayoutRelationEqual
                                                                   toItem:[[self window] contentView]
                                                                attribute:NSLayoutAttributeTop
                                                               multiplier:1
                                                                 constant:topgap];
    m_PanelConstraints.left_bottom = [NSLayoutConstraint constraintWithItem:m_LeftPanelView
                                                                attribute:NSLayoutAttributeBottom
                                                                relatedBy:NSLayoutRelationEqual
                                                                   toItem:[[self window] contentView]
                                                                attribute:NSLayoutAttributeBottom
                                                               multiplier:1
                                                                 constant:0];
    m_PanelConstraints.left_right = [NSLayoutConstraint constraintWithItem:m_LeftPanelView
                                                               attribute:NSLayoutAttributeRight
                                                               relatedBy:NSLayoutRelationEqual
                                                                  toItem:[[self window] contentView]
                                                               attribute:NSLayoutAttributeCenterX
                                                              multiplier:1
                                                                constant:0];
    m_PanelConstraints.right_left = [NSLayoutConstraint constraintWithItem:m_RightPanelView
                                                                attribute:NSLayoutAttributeLeft
                                                                relatedBy:NSLayoutRelationEqual
                                                                   toItem:[[self window] contentView]
                                                                attribute:NSLayoutAttributeCenterX
                                                               multiplier:1
                                                                 constant:0];
    m_PanelConstraints.right_top = [NSLayoutConstraint constraintWithItem:m_RightPanelView
                                                               attribute:NSLayoutAttributeTop
                                                               relatedBy:NSLayoutRelationEqual
                                                                  toItem:[[self window] contentView]
                                                               attribute:NSLayoutAttributeTop
                                                              multiplier:1
                                                                constant:topgap];
    m_PanelConstraints.right_bottom = [NSLayoutConstraint constraintWithItem:m_RightPanelView
                                                                  attribute:NSLayoutAttributeBottom
                                                                  relatedBy:NSLayoutRelationEqual
                                                                     toItem:[[self window] contentView]
                                                                  attribute:NSLayoutAttributeBottom
                                                                 multiplier:1
                                                                   constant:0];
    m_PanelConstraints.right_right = [NSLayoutConstraint constraintWithItem:m_RightPanelView
                                                                 attribute:NSLayoutAttributeRight
                                                                 relatedBy:NSLayoutRelationEqual
                                                                    toItem:[[self window] contentView]
                                                                 attribute:NSLayoutAttributeRight
                                                                multiplier:1
                                                                  constant:0];
    [[[self window] contentView] addConstraint:m_PanelConstraints.left_left];
    [[[self window] contentView] addConstraint:m_PanelConstraints.left_top];
    [[[self window] contentView] addConstraint:m_PanelConstraints.left_right];
    [[[self window] contentView] addConstraint:m_PanelConstraints.left_bottom];    
    [[[self window] contentView] addConstraint:m_PanelConstraints.right_left];
    [[[self window] contentView] addConstraint:m_PanelConstraints.right_top];
    [[[self window] contentView] addConstraint:m_PanelConstraints.right_right];
    [[[self window] contentView] addConstraint:m_PanelConstraints.right_bottom];

    [self UpdatePanelConstraints:[[self window] frame].size];
}

- (void)UpdatePanelConstraints: (NSSize)frameSize
{
    float gran = 9.;
    float center_x = frameSize.width / 2.;
    float rest = fmod(center_x, gran);
    m_PanelConstraints.left_right.constant = -rest+1;
    m_PanelConstraints.right_left.constant = -rest;

    [[[self window] contentView] setNeedsLayout:true];
}

- (void)windowDidResize:(NSNotification *)notification
{
    [self UpdatePanelConstraints:[[self window] frame].size];    
}

- (void)windowWillClose:(NSNotification *)notification
{
    [(AppDelegate*)[NSApp delegate] RemoveMainWindow:self];
}

- (void)DidBecomeKeyWindow
{
    // update key modifiers state for views    
    unsigned long flags = [NSEvent modifierFlags];
    [m_LeftPanelController ModifierFlagsChanged:flags];
    [m_RightPanelController ModifierFlagsChanged:flags];
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (bool) IsPanelActive
{
    return m_ActiveState == StateLeftPanel || m_ActiveState == StateRightPanel;
}

- (PanelView*) ActivePanelView
{
    if(m_ActiveState == StateLeftPanel)
    {
        return m_LeftPanelView;
    }
    else if(m_ActiveState == StateRightPanel)
    {
        return m_RightPanelView;
    }
    assert(0);
    return 0;
}

- (PanelData*) ActivePanelData
{
    if(m_ActiveState == StateLeftPanel)
    {
        return m_LeftPanelData;
    }
    else if(m_ActiveState == StateRightPanel)
    {
        return m_RightPanelData;
    }
    assert(0);
    return 0;
}

- (PanelController*) ActivePanelController
{
    if(m_ActiveState == StateLeftPanel)
    {
        return m_LeftPanelController;
    }
    else if(m_ActiveState == StateRightPanel)
    {
        return m_RightPanelController;
    }
    assert(0);
    return 0;
}

- (void) HandleTabButton
{
    if(m_ActiveState == StateLeftPanel)
    {
        m_ActiveState = StateRightPanel;
        [m_RightPanelView Activate];
        [m_LeftPanelView Disactivate];
    }
    else
    {
        m_ActiveState = StateLeftPanel;
        [m_LeftPanelView Activate];
        [m_RightPanelView Disactivate];
    }
}

- (void) FireDirectoryChanged: (const char*) _dir ticket:(unsigned long)_ticket
{
    [m_LeftPanelController FireDirectoryChanged:_dir ticket:_ticket];
    [m_RightPanelController FireDirectoryChanged:_dir ticket:_ticket];
}

- (void)keyDown:(NSEvent *)event
{
    NSString*  const character = [event charactersIgnoringModifiers];
    if ( [character length] != 1 ) return;
    unichar const unicode        = [character characterAtIndex:0];
    unsigned short const keycode = [event keyCode];
    NSUInteger const modif       = [event modifierFlags];
#define ISMODIFIER(_v) ( (modif&NSDeviceIndependentModifierFlagsMask) == (_v) )

    if([self IsPanelActive])
        [[self ActivePanelController] keyDown:event]; 
    
    switch (unicode)
    {
        case NSTabCharacter: // TAB key
            [self HandleTabButton];
            break;
    };
    
    switch (keycode)
    {
        case 17: // t button on keyboard
        {
            if(ISMODIFIER(NSCommandKeyMask|NSAlternateKeyMask|NSControlKeyMask|NSShiftKeyMask))
            {
                [m_OperationsController AddOperation:
                 [[TimedDummyOperation alloc] initWithTime:(1 + rand()%10)]];
            }
            break;
        }
    }
#undef ISMODIFIER
}

- (void)flagsChanged:(NSEvent *)theEvent
{
    if([self IsPanelActive])
    {
        unsigned long flags = [theEvent modifierFlags];
        [m_LeftPanelController ModifierFlagsChanged:flags];
        [m_RightPanelController ModifierFlagsChanged:flags];
    }
}

- (IBAction)LeftPanelGoToButtonAction:(id)sender{
    [m_LeftPanelController GoToDirectory:[[[self LeftPanelGoToButton] GetCurrentSelectionPath] UTF8String]];
}

- (IBAction)RightPanelGoToButtonAction:(id)sender{
    [m_RightPanelController GoToDirectory:[[[self RightPanelGoToButton] GetCurrentSelectionPath] UTF8String]];
}

- (IBAction)ToggleShortViewMode:(id)sender {
    [[self ActivePanelController] ToggleShortViewMode];
}

- (IBAction)ToggleMediumViewMode:(id)sender {
    [[self ActivePanelController] ToggleMediumViewMode];
}

- (IBAction)ToggleFullViewMode:(id)sender{
    [[self ActivePanelController] ToggleFullViewMode];
}

- (IBAction)ToggleWideViewMode:(id)sender{
    [[self ActivePanelController] ToggleWideViewMode];
}

- (IBAction)ToggleSortByName:(id)sender{
    [[self ActivePanelController] ToggleSortingByName];
}

- (IBAction)ToggleSortByExt:(id)sender{
    [[self ActivePanelController] ToggleSortingByExt];
}

- (IBAction)ToggleSortByMTime:(id)sender{
    [[self ActivePanelController] ToggleSortingByMTime];
}

- (IBAction)ToggleSortBySize:(id)sender{
    [[self ActivePanelController] ToggleSortingBySize];
}

- (IBAction)ToggleSortByBTime:(id)sender{
    [[self ActivePanelController] ToggleSortingByBTime];
}

- (IBAction)ToggleViewHiddenFiles:(id)sender{
    [[self ActivePanelController] ToggleViewHiddenFiles];    
}

- (IBAction)ToggleSeparateFoldersFromFiles:(id)sender{
    [[self ActivePanelController] ToggleSeparateFoldersFromFiles];
}

- (IBAction)LeftPanelGoto:(id)sender{
    [[self LeftPanelGoToButton] performClick:self];    
}

- (IBAction)RightPanelGoto:(id)sender{
    [[self RightPanelGoToButton] performClick:self];
}

- (IBAction)OnSyncPanels:(id)sender{
    assert([self IsPanelActive]);
    char dirpath[__DARWIN_MAXPATHLEN];
    if(m_ActiveState == StateLeftPanel)
    {
        m_LeftPanelData->GetDirectoryPathWithTrailingSlash(dirpath);
        [m_RightPanelController GoToDirectory:dirpath];
    }
    else
    {
        m_RightPanelData->GetDirectoryPathWithTrailingSlash(dirpath);
        [m_LeftPanelController GoToDirectory:dirpath];
    }
}

- (IBAction)OnSwapPanels:(id)sender{
    assert([self IsPanelActive]);
    std::swap(m_LeftPanelView, m_RightPanelView);
    std::swap(m_LeftPanelData, m_RightPanelData);
    std::swap(m_LeftPanelController, m_RightPanelController);
    if(m_ActiveState == StateLeftPanel) m_ActiveState = StateRightPanel;
    else if(m_ActiveState == StateRightPanel) m_ActiveState = StateLeftPanel;
    [self CreatePanelConstraints];    
}

- (IBAction)OnRefreshPanel:(id)sender{
    assert([self IsPanelActive]);
    [[self ActivePanelController] RefreshDirectory];
}

- (IBAction)OnFileAttributes:(id)sender{
    assert([self IsPanelActive]);
    FileSysEntryAttrSheetController *sheet = [FileSysEntryAttrSheetController new];
    FileSysEntryAttrSheetCompletionHandler handler = ^(int result){
        if(result == DialogResult::Apply)
        {
            FileSysAttrAlterCommand *command = [sheet Result];
            [m_OperationsController AddOperation:[[FileSysAttrChangeOperation alloc] initWithCommand:command]];
        }
    };

    if([self ActivePanelData]->GetSelectedItemsCount() > 0 )
    {
        [sheet ShowSheet:[self window] selentries:[self ActivePanelData] handler:handler];
    }
    else
    {
        PanelView *curview = [self ActivePanelView];
        PanelData *curdata = [self ActivePanelData];
        int curpos = [curview GetCursorPosition];
        int rawpos = curdata->SortPosToRawPos(curpos);
        if(!curdata->EntryAtRawPosition(rawpos).isdotdot())
            [sheet ShowSheet:[self window] data:[self ActivePanelData] index:rawpos handler:handler];
    }
}

- (IBAction)OnDetailedVolumeInformation:(id)sender{
    assert([self IsPanelActive]);
    PanelView *curview = [self ActivePanelView];
    PanelData *curdata = [self ActivePanelData];
    int curpos = [curview GetCursorPosition];
    int rawpos = curdata->SortPosToRawPos(curpos);
    char src[__DARWIN_MAXPATHLEN];
    curdata->ComposeFullPathForEntry(rawpos, src);
    
    DetailedVolumeInformationSheetController *sheet = [DetailedVolumeInformationSheetController new];
    [sheet ShowSheet:[self window] destpath:src];
}

- (IBAction)OnDeleteCommand:(id)sender{
    assert([self IsPanelActive]);
    
    __block FlexChainedStringsChunk *files = 0;
    if([self ActivePanelData]->GetSelectedItemsCount() > 0 )
    {
        files = [self ActivePanelData]->StringsFromSelectedEntries();
    }
    else
    {
        int curpos = [[self ActivePanelView] GetCursorPosition];
        int rawpos = [self ActivePanelData]->SortPosToRawPos(curpos);
        auto const &item = [self ActivePanelData]->EntryAtRawPosition(rawpos);
        if(!item.isdotdot()) // do not try to delete a parent directory
            files = FlexChainedStringsChunk::AllocateWithSingleString(item.namec());
    }
    
    if(!files)
        return;
    
    
    FileDeletionSheetController *sheet = [[FileDeletionSheetController alloc] init];
    [sheet ShowSheet:self.window Files:files Type:FileDeletionOperationType::MoveToTrash
             Handler:^(int result){
        if (result == DialogResult::Delete)
        {
            FileDeletionOperationType type = [sheet GetType];
            
            char root_path[MAXPATHLEN];
            [self ActivePanelData]->GetDirectoryPathWithTrailingSlash(root_path);

            FileDeletionOperation *op = [[FileDeletionOperation alloc]
                                         initWithFiles:files
                                         type:type
                                         rootpath:root_path];
            [m_OperationsController AddOperation:op];
        }
        else
        {
            FlexChainedStringsChunk::FreeWithDescendants(&files);
        }
    }];
}

- (IBAction)OnCreateDirectoryCommand:(id)sender{
    assert([self IsPanelActive]);
    CreateDirectorySheetController *cd = [[CreateDirectorySheetController alloc] init];
    [cd ShowSheet:[self window] handler:^(int _ret)
     {
         if(_ret == DialogResult::Create)
         {             
             PanelData *curdata = [self ActivePanelData];
             char pdir[MAXPATHLEN];
             curdata->GetDirectoryPath(pdir);
             
             [m_OperationsController AddOperation:[[CreateDirectoryOperation alloc] initWithPath:[[cd.TextField stringValue] UTF8String]
                                                                                        rootpath:pdir
                                                   ]];
         }
     }];
}

- (IBAction)OnFileCopyCommand:(id)sender{
    assert([self IsPanelActive]);
    const PanelData *source, *destination;
    if(m_ActiveState == StateLeftPanel)
    {
        source = m_LeftPanelData;
        destination = m_RightPanelData;
    }
    else
    {
        source = m_RightPanelData;
        destination = m_LeftPanelData;
    }
    
    __block FlexChainedStringsChunk *files = 0;
    if(source->GetSelectedItemsCount() > 0 )
    {
        files = source->StringsFromSelectedEntries();
    }
    else
    {
        int curpos = [[self ActivePanelView] GetCursorPosition];
        int rawpos = [self ActivePanelData]->SortPosToRawPos(curpos);
        auto const &item = [self ActivePanelData]->EntryAtRawPosition(rawpos);
        if(!item.isdotdot()) // do not try to copy a parent directory
            files = FlexChainedStringsChunk::AllocateWithSingleString(item.namec());
    }
    
    if(!files)
        return;

    char dest_path[MAXPATHLEN];
    destination->GetDirectoryPathWithTrailingSlash(dest_path);
    NSString *nsdirpath = [NSString stringWithUTF8String:dest_path];
    MassCopySheetController *mc = [[MassCopySheetController alloc] init];
    [mc ShowSheet:[self window] initpath:nsdirpath iscopying:true handler:^(int _ret)
     {
         if(_ret == DialogResult::Copy)
         {
             char root_path[MAXPATHLEN];
             source->GetDirectoryPathWithTrailingSlash(root_path);
             
             FileCopyOperationOptions opts;
             
             [m_OperationsController AddOperation:
              [[FileCopyOperation alloc] initWithFiles:files root:root_path dest:[[mc.TextField stringValue] UTF8String] options:&opts]];
         }
         else
         {
             FlexChainedStringsChunk::FreeWithDescendants(&files);
         
         }
     }];
}

- (IBAction)OnFileCopyAsCommand:(id)sender{
    // process only current cursor item
    assert([self IsPanelActive]);
    
    int curpos = [[self ActivePanelView] GetCursorPosition];
    int rawpos = [self ActivePanelData]->SortPosToRawPos(curpos);
    auto const &item = [self ActivePanelData]->EntryAtRawPosition(rawpos);
    if(item.isdotdot())
        return;
    __block FlexChainedStringsChunk *files = FlexChainedStringsChunk::AllocateWithSingleString(item.namec());
    
    MassCopySheetController *mc = [[MassCopySheetController alloc] init];
    [mc ShowSheet:[self window] initpath:[NSString stringWithUTF8String:item.namec()] iscopying:true handler:^(int _ret)
     {
         if(_ret == DialogResult::Copy)
         {
             char root_path[MAXPATHLEN];
             [self ActivePanelData]->GetDirectoryPathWithTrailingSlash(root_path);
             FileCopyOperationOptions opts;
             
             [m_OperationsController AddOperation:
              [[FileCopyOperation alloc] initWithFiles:files
                                                  root:root_path
                                                  dest:[[mc.TextField stringValue] UTF8String]
                                               options:&opts]];
         }
         else
         {
             FlexChainedStringsChunk::FreeWithDescendants(&files);
         }
     }];
}

- (IBAction)OnFileRenameMoveCommand:(id)sender{
    assert([self IsPanelActive]);
    const PanelData *source, *destination;
    if(m_ActiveState == StateLeftPanel)
    {
        source = m_LeftPanelData;
        destination = m_RightPanelData;
    }
    else
    {
        source = m_RightPanelData;
        destination = m_LeftPanelData;
    }
    
    __block FlexChainedStringsChunk *files = 0;
    if(source->GetSelectedItemsCount() > 0 )
    {
        files = source->StringsFromSelectedEntries();
    }
    else
    {
        int curpos = [[self ActivePanelView] GetCursorPosition];
        int rawpos = [self ActivePanelData]->SortPosToRawPos(curpos);
        auto const &item = [self ActivePanelData]->EntryAtRawPosition(rawpos);
        if(!item.isdotdot()) // do not try to rename a parent directory
            files = FlexChainedStringsChunk::AllocateWithSingleString(item.namec());
    }
    
    if(!files)
        return;        
    
    char dest_path[MAXPATHLEN];
    destination->GetDirectoryPathWithTrailingSlash(dest_path);
    NSString *nsdirpath = [NSString stringWithUTF8String:dest_path];

    MassCopySheetController *mc = [[MassCopySheetController alloc] init];
    [mc ShowSheet:[self window] initpath:nsdirpath iscopying:false handler:^(int _ret)
     {
         if(_ret == DialogResult::Copy)
         {             
             char root_path[MAXPATHLEN];
             source->GetDirectoryPathWithTrailingSlash(root_path);
             
             FileCopyOperationOptions opts;
             opts.docopy = false;
             
             [m_OperationsController AddOperation:
              [[FileCopyOperation alloc] initWithFiles:files root:root_path dest:[[mc.TextField stringValue] UTF8String] options:&opts]];
         }
         else
         {
             FlexChainedStringsChunk::FreeWithDescendants(&files);           
         }
     }];    
}

- (IBAction)OnFileRenameMoveAsCommand:(id)sender {
    
    // process only current cursor item
    assert([self IsPanelActive]);
    
    int curpos = [[self ActivePanelView] GetCursorPosition];
    int rawpos = [self ActivePanelData]->SortPosToRawPos(curpos);
    auto const &item = [self ActivePanelData]->EntryAtRawPosition(rawpos);
    if(item.isdotdot())
        return;
    
    __block FlexChainedStringsChunk *files = FlexChainedStringsChunk::AllocateWithSingleString(item.namec());
    
    MassCopySheetController *mc = [[MassCopySheetController alloc] init];
    [mc ShowSheet:[self window] initpath:[NSString stringWithUTF8String:item.namec()] iscopying:false handler:^(int _ret)
     {
         if(_ret == DialogResult::Copy)
         {
             char root_path[MAXPATHLEN];
             [self ActivePanelData]->GetDirectoryPathWithTrailingSlash(root_path);
             FileCopyOperationOptions opts;
             opts.docopy = false;
             
             [m_OperationsController AddOperation:
              [[FileCopyOperation alloc] initWithFiles:files
                                                  root:root_path
                                                  dest:[[mc.TextField stringValue] UTF8String]
                                               options:&opts]];
         }
         else
         {
             FlexChainedStringsChunk::FreeWithDescendants(&files);
         }
     }];
}

@end
