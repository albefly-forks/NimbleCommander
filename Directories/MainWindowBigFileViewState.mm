//
//  MainWindowBigFileViewState.m
//  Files
//
//  Created by Michael G. Kazakov on 04.06.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import "MainWindowBigFileViewState.h"
#import "BigFileView.h"
#import "FileWindow.h"
#import "MainWindowController.h"
#import "Common.h"
#import "SearchInFile.h"

static NSMutableDictionary *EncodingToDict(int _encoding, NSString *_name)
{
    return [NSMutableDictionary dictionaryWithObjectsAndKeys:
            _name, @"name",
            [NSNumber numberWithInt:_encoding], @"code",
            nil
            ];
}

@implementation MainWindowBigFileViewState
{
    FileWindow  *m_FileWindow;
    FileWindow  *m_SearchFileWindow;
    SearchInFile *m_SearchInFile;
    
    BigFileView *m_View;
    NSPopUpButton *m_EncodingSelect;
    NSMutableArray *m_Encodings;
    NSButton    *m_WordWrap;
    NSPopUpButton *m_ModeSelect;
    NSTextField *m_FileSize;
    NSTextField *m_ScrollPosition;

    NSSearchField *m_SearchField;
    NSProgressIndicator *m_SearchIndicator;
    NSTextField   *m_SearchResult;
    bool           m_IsStopSearching;
    bool           m_IsSearchRunning;

    char        m_FilePath[MAXPATHLEN];
}

- (id)initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if(self)
    {
        m_IsStopSearching = false;
        m_IsSearchRunning = false;
        m_Encodings = [NSMutableArray new];
        [self CreateControls];
    }
    return self;
}

- (void) dealloc
{
    if(m_SearchInFile != 0)
    {
        delete m_SearchInFile;
    }    
    if(m_FileWindow != 0)
    {
        if(m_FileWindow->FileOpened())
            m_FileWindow->CloseFile();
        delete m_FileWindow;
        m_FileWindow = 0;
    }
    if(m_SearchFileWindow != 0)
    {
        if(m_SearchFileWindow->FileOpened())
            m_SearchFileWindow->CloseFile();
        delete m_SearchFileWindow;
        m_SearchFileWindow = 0;
    }
}

- (NSView*) ContentView
{
    return self;
}

- (void) Assigned
{
    [self.window makeFirstResponder:m_View];
    [self UpdateTitle];    
}

- (void) Resigned
{
    [m_View SetDelegate:nil];
    [m_View DoClose];
    m_IsStopSearching = true;
}

- (void) UpdateTitle
{
    NSString *path = [NSString stringWithUTF8String:m_FilePath];
    
    // find window geometry
    NSWindow* window = [self window];
    float leftEdge = NSMaxX([[window standardWindowButton:NSWindowZoomButton] frame]);
    NSButton* fsbutton = [window standardWindowButton:NSWindowFullScreenButton];
    float rightEdge = fsbutton ? [fsbutton frame].origin.x : NSMaxX([window frame]);
    
    // Leave 8 pixels of padding around the title.
    const int kTitlePadding = 8;
    float titleWidth = rightEdge - leftEdge - 2 * kTitlePadding;
    
    // Sending |titleBarFontOfSize| 0 returns default size
    NSDictionary* attributes = [NSDictionary dictionaryWithObject:[NSFont titleBarFontOfSize:0] forKey:NSFontAttributeName];
    window.title = StringByTruncatingToWidth(path, titleWidth, kTruncateAtStart, attributes);
    
}

- (void)cancelOperation:(id)sender
{
    [(MainWindowController*)[[self window] delegate] ResignAsWindowState:self];
}

- (bool) OpenFile: (const char*) _fn
{    
    FileWindow *fw = new FileWindow;
    if(fw->OpenFile(_fn) == 0)
    {
        if(m_FileWindow != 0)
        {
            if(m_FileWindow->FileOpened())
                m_FileWindow->CloseFile();
            delete m_FileWindow;
            m_FileWindow = 0;
        }
        
        m_FileWindow = fw;
        m_SearchFileWindow = new FileWindow;
        m_SearchFileWindow->OpenFile(_fn);        
        m_SearchInFile = new SearchInFile(m_SearchFileWindow);
        
        strcpy(m_FilePath, _fn);
        [m_View SetFile:m_FileWindow];
        
        // update UI
        [self SelectEncodingFromView];
        [self SelectModeFromView];
        [self UpdateWordWrap];
        [self BigFileViewScrolled];
        
        return true;
    }
    else
    {
        delete fw;
        return false;
    }
}

- (void) CreateControls
{
    m_View = [[BigFileView alloc] initWithFrame:self.frame];
    [m_View setTranslatesAutoresizingMaskIntoConstraints:NO];
    [m_View SetDelegate:self];
    [self addSubview:m_View];
    
    m_EncodingSelect = [[NSPopUpButton alloc] initWithFrame:NSRect()];
    [m_EncodingSelect setTranslatesAutoresizingMaskIntoConstraints:NO];
    [(NSPopUpButtonCell*)[m_EncodingSelect cell] setControlSize:NSSmallControlSize];
    [m_EncodingSelect setTarget:self];
    [m_EncodingSelect setAction:@selector(SelectedEncoding:)];
    [m_EncodingSelect setFont:[NSFont menuFontOfSize:10]];
    [self addSubview:m_EncodingSelect];
    
    m_ModeSelect = [[NSPopUpButton alloc] initWithFrame:NSRect()];
    [m_ModeSelect setTranslatesAutoresizingMaskIntoConstraints:NO];
    [(NSPopUpButtonCell*)[m_ModeSelect cell] setControlSize:NSSmallControlSize];
    [m_ModeSelect setTarget:self];
    [m_ModeSelect setAction:@selector(SelectMode:)];
    [m_ModeSelect addItemWithTitle:@"Text"];
    [m_ModeSelect addItemWithTitle:@"Hex"];
    [m_ModeSelect setFont:[NSFont menuFontOfSize:10]];
    [self addSubview:m_ModeSelect];
    
    m_WordWrap = [[NSButton alloc] initWithFrame:NSRect()];
    [m_WordWrap setTranslatesAutoresizingMaskIntoConstraints:NO];
    [[m_WordWrap cell] setControlSize:NSSmallControlSize];
    [m_WordWrap setButtonType:NSSwitchButton];
    [m_WordWrap setTitle:@"Word wrap"];
    [m_WordWrap setTarget:self];
    [m_WordWrap setAction:@selector(WordWrapChanged:)];
    [self addSubview:m_WordWrap];
    
    m_ScrollPosition = [[NSTextField alloc] initWithFrame:NSRect()];
    [m_ScrollPosition setTranslatesAutoresizingMaskIntoConstraints:NO];
    [m_ScrollPosition setEditable:false];
    [m_ScrollPosition setBordered:false];
    [m_ScrollPosition setDrawsBackground:false];
    [self addSubview:m_ScrollPosition];
    
    m_SearchField = [[NSSearchField alloc]initWithFrame:NSRect()];
    [m_SearchField setTranslatesAutoresizingMaskIntoConstraints:NO];
    [m_SearchField setTarget:self];
    [m_SearchField setAction:@selector(UpdateSearchFilter:)];
    [[m_SearchField cell] setPlaceholderString:@"Search in file"];
    [[m_SearchField cell] setSendsWholeSearchString:true];
    [self addSubview:m_SearchField];
    
    m_SearchIndicator = [[NSProgressIndicator alloc] initWithFrame:NSRect()];
    [m_SearchIndicator setIndeterminate:YES];
    [m_SearchIndicator setStyle:NSProgressIndicatorSpinningStyle];
    [m_SearchIndicator setTranslatesAutoresizingMaskIntoConstraints:NO];
    [m_SearchIndicator setControlSize:NSSmallControlSize];
    [m_SearchIndicator setDisplayedWhenStopped:NO];
    [self addSubview:m_SearchIndicator];
    
    m_SearchResult = [[NSTextField alloc] initWithFrame:NSRect()];
    [m_SearchResult setTranslatesAutoresizingMaskIntoConstraints:NO];
    [m_SearchResult setEditable:false];
    [m_SearchResult setBordered:false];
    [m_SearchResult setDrawsBackground:false];
    [m_SearchResult setStringValue:@""];
    [self addSubview:m_SearchResult];
        
    NSBox *line = [[NSBox alloc] initWithFrame:NSRect()];
    [line setTranslatesAutoresizingMaskIntoConstraints:NO];
    [line setBoxType:NSBoxSeparator];
    [self addSubview:line];

    NSDictionary *views = NSDictionaryOfVariableBindings(m_View, m_EncodingSelect, m_WordWrap, m_ModeSelect, m_ScrollPosition, m_SearchField, m_SearchIndicator, m_SearchResult, line);
    [self addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"|-(<=1)-[m_View]-(<=1)-|" options:0 metrics:nil views:views]];
    [self addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"|-(==0)-[line]-(==0)-|" options:0 metrics:nil views:views]];
    [self addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
                          @"|-[m_EncodingSelect]-[m_ModeSelect]-[m_WordWrap]-[m_ScrollPosition]"
                                                                 options:NSLayoutFormatAlignAllCenterY metrics:nil views:views]];
    [self addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"[m_SearchIndicator]-[m_SearchField(200)]-|"
                                                                 options:NSLayoutFormatAlignAllCenterY metrics:nil views:views]];
    [self addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"[m_SearchResult]-[m_SearchField]"
                                                                 options:NSLayoutFormatAlignAllCenterY metrics:nil views:views]];
    [self addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|-[m_SearchField]" options:0 metrics:nil views:views]];
    [self addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|-[m_EncodingSelect(18)]-[line(<=1)]-(==0)-[m_View]-(<=1)-|" options:0 metrics:nil views:views]];
    
    [self FillEncodingSelection];
}

- (void) FillEncodingSelection
{
    [m_Encodings addObject:EncodingToDict(ENCODING_MACOS_ROMAN_WESTERN, @"Western (Mac OS Roman)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_OEM866, @"OEM 866 (DOS)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_WIN1251, @"Windows 1251")];
    [m_Encodings addObject:EncodingToDict(ENCODING_UTF8, @"UTF-8")];
    [m_Encodings addObject:EncodingToDict(ENCODING_UTF16LE, @"UTF-16 LE")];
    [m_Encodings addObject:EncodingToDict(ENCODING_UTF16BE, @"UTF-16 BE")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_1, @"Western (ISO Latin 1)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_2, @"Central European (ISO Latin 2)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_3, @"Western (ISO Latin 3)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_4, @"Central European (ISO Latin 4)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_5, @"Cyrillic (ISO 8859-5)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_6, @"Arabic (ISO 8859-6)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_7, @"Greek (ISO 8859-7)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_8, @"Hebrew (ISO 8859-8)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_9, @"Turkish (ISO Latin 5)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_10, @"Nordic (ISO Latin 6)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_11, @"Thai (ISO 8859-11)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_13, @"Baltic (ISO Latin 7)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_14, @"Celtic (ISO Latin 8)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_15, @"Western (ISO Latin 9)")];
    [m_Encodings addObject:EncodingToDict(ENCODING_ISO_8859_16, @"Romanian (ISO Latin 10)")];
    
    for(NSMutableDictionary *d in m_Encodings)
        [m_EncodingSelect addItemWithTitle:[d objectForKey:@"name"]];
}

- (void) SelectEncodingFromView
{
    int current_encoding = [m_View Enconding];
    for(NSMutableDictionary *d in m_Encodings)
        if([(NSNumber*)[d objectForKey:@"code"] intValue] == current_encoding)
        {
            [m_EncodingSelect selectItemWithTitle:[d objectForKey:@"name"]];
            break;
        }
}

- (void) SelectedEncoding:(id)sender
{
    for(NSMutableDictionary *d in m_Encodings)
        if([[d objectForKey:@"name"] isEqualToString:[[m_EncodingSelect selectedItem] title]])
        {
            [m_View SetEncoding:[(NSNumber*)[d objectForKey:@"code"] intValue]];
            [self UpdateSearchFilter:self];
            break;
        }
}

- (void) UpdateWordWrap
{
    [m_WordWrap setState:[m_View WordWrap] ? NSOnState : NSOffState];
    [m_WordWrap setEnabled:[m_View Mode] == BigFileViewModes::Text];
}

- (void) WordWrapChanged:(id)sender
{
    [m_View SetWordWrap: [m_WordWrap state]==NSOnState];
}

- (void) SelectModeFromView
{
    if([m_View Mode] == BigFileViewModes::Text)
        [m_ModeSelect selectItemAtIndex:0];
    else if([m_View Mode] == BigFileViewModes::Hex)
        [m_ModeSelect selectItemAtIndex:1];
    else
        assert(0);
}

- (void) SelectMode:(id)sender
{
    if([m_ModeSelect indexOfSelectedItem] == 0)
        [m_View SetMode:BigFileViewModes::Text];
    else if([m_ModeSelect indexOfSelectedItem] == 1)
        [m_View SetMode:BigFileViewModes::Hex];
    [self UpdateWordWrap];
}

- (void) BigFileViewScrolled
{
    NSString *s = [NSString stringWithFormat:@"%zub %.0f%%",
                   m_FileWindow->FileSize(),
                   [m_View VerticalScrollPosition]*100.];
    [m_ScrollPosition setStringValue:s];
}

- (void) BigFileViewScrolledByUser
{
    m_SearchInFile->MoveCurrentPosition([m_View VerticalPositionInBytes]);     
}

- (void)UpdateSearchFilter:sender
{
    NSString *str = [m_SearchField stringValue];
    if([str length] == 0)
    {
        m_IsStopSearching = true; // we should stop current search if any
        [m_SearchResult setStringValue:@""];
        [m_View SetSelectionInFile:CFRangeMake(-1, 0)];
        return;
    }
    
    if( m_SearchInFile->TextSearchString() == NULL ||
       [str compare:(__bridge NSString*) m_SearchInFile->TextSearchString()] != NSOrderedSame ||
       m_SearchInFile->TextSearchEncoding() != [m_View Enconding] )
    { // user did some changes in search request
        [m_SearchResult setStringValue:@""];
        [m_View SetSelectionInFile:CFRangeMake(-1, 0)]; // remove current selection
        
        uint64_t offset = [m_View VerticalPositionInBytes];
        int encoding = [m_View Enconding];
        
        m_IsStopSearching = true; // we should stop current search if any
        dispatch_async(m_SearchInFile->Queue(), ^{
            m_IsStopSearching = false;
            m_SearchInFile->MoveCurrentPosition(offset);
            m_SearchInFile->ToggleTextSearch((CFStringRef) CFBridgingRetain(str), encoding);
        });
    }
    else
    { // request is the same
        if(m_IsSearchRunning)
            return; // we're already performing this request now, nothing to do
    }
    
    dispatch_async(m_SearchInFile->Queue(), ^{
        m_IsStopSearching = false;
        uint64_t offset, len;
        dispatch_async(dispatch_get_main_queue(), ^{[self NotifySearching:true];});
        auto result = m_SearchInFile->Search(&offset, &len, ^{return m_IsStopSearching;});
        dispatch_async(dispatch_get_main_queue(), ^{[self NotifySearching:false];});
        
        if(result == SearchInFile::Result::Found)
            dispatch_async(dispatch_get_main_queue(), ^{
                [m_View SetSelectionInFile:CFRangeMake(offset, len)];
                [m_View ScrollToSelection];
            });
        else if(result == SearchInFile::Result::NotFound)            
            [m_SearchResult setStringValue:@"Not found"];
    });
}

- (void)NotifySearching: (bool) _is_running
{
    if(m_IsSearchRunning == _is_running)
        return;
    m_IsSearchRunning = _is_running;
    const auto visual_spinning_delay = 100ull; // should be 100 ms of workload before user will get spinning indicator
    
    if(m_IsSearchRunning)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, visual_spinning_delay * USEC_PER_SEC), dispatch_get_main_queue(), ^{
                           if(m_IsSearchRunning) // need to check if task was already done
                               [m_SearchIndicator startAnimation:self];
                       });
    else
        [m_SearchIndicator stopAnimation:self];
}

- (void)performFindPanelAction:(id)sender
{
    [self.window makeFirstResponder:m_SearchField];
}

@end
