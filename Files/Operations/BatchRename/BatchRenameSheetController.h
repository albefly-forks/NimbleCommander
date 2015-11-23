//
//  BatchRenameSheetController.h
//  Files
//
//  Created by Michael G. Kazakov on 16/05/15.
//  Copyright (c) 2015 Michael G. Kazakov. All rights reserved.
//

#pragma once

#include "../../vfs/VFS.h"
#include "../../SheetController.h"

@interface BatchRenameSheetController : SheetController<NSTableViewDataSource,NSTableViewDelegate,NSTextFieldDelegate,NSComboBoxDelegate>
- (instancetype) initWithItems:(vector<VFSListingItem>)_items;

- (IBAction)OnCancel:(id)sender;

@property (strong) IBOutlet NSTableView *FilenamesTable;
@property (strong) IBOutlet NSComboBox *FilenameMask;
@property (strong) IBOutlet NSComboBox *SearchForComboBox;
@property (strong) IBOutlet NSComboBox *ReplaceWithComboBox;
@property (strong) IBOutlet NSButton *SearchCaseSensitive;
@property (strong) IBOutlet NSButton *SearchOnlyOnce;
@property (strong) IBOutlet NSButton *SearchInExtension;
@property (strong) IBOutlet NSButton *SearchWithRegExp;
@property (strong) IBOutlet NSPopUpButton *CaseProcessing;
@property (strong) IBOutlet NSButton *CaseProcessingWithExtension;
@property (strong) IBOutlet NSPopUpButton *CounterDigits;
@property (strong) IBOutlet NSButton *InsertNameRangePlaceholderButton;
@property (strong) IBOutlet NSButton *InsertPlaceholderMenuButton;
@property (strong) IBOutlet NSMenu *InsertPlaceholderMenu;
@property (nonatomic, readwrite) int CounterStartsAt;
@property (nonatomic, readwrite) int CounterStepsBy;

@property (readonly) vector<string> &filenamesSource;       // full path
@property (readonly) vector<string> &filenamesDestination;
@property bool isValidRenaming;
@property (strong) IBOutlet NSButton *OkButton;


- (IBAction)OnFilenameMaskChanged:(id)sender;
- (IBAction)OnInsertNamePlaceholder:(id)sender;
- (IBAction)OnInsertNameRangePlaceholder:(id)sender;
- (IBAction)OnInsertCounterPlaceholder:(id)sender;
- (IBAction)OnInsertExtensionPlaceholder:(id)sender;
- (IBAction)OnInsertDatePlaceholder:(id)sender;
- (IBAction)OnInsertTimePlaceholder:(id)sender;
- (IBAction)OnInsertMenu:(id)sender;
- (IBAction)OnInsertPlaceholderFromMenu:(id)sender;

- (IBAction)OnSearchForChanged:(id)sender;
- (IBAction)OnReplaceWithChanged:(id)sender;
- (IBAction)OnSearchReplaceOptionsChanged:(id)sender;
- (IBAction)OnCaseProcessingChanged:(id)sender;
- (IBAction)OnCounterSettingsChanged:(id)sender;

- (IBAction)OnOK:(id)sender;

@end