//
//  PreferencesWindowTerminalTab.m
//  Files
//
//  Created by Michael G. Kazakov on 10.06.14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#import "PreferencesWindowTerminalTab.h"
#include "Config.h"
#include "Common.h"

static const auto g_ConfigFont = "terminal.font";

@implementation PreferencesWindowTerminalTab
{
    NSFont *m_Font;
}

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil
{
    self = [super initWithNibName:NSStringFromClass(self.class) bundle:nibBundleOrNil];
    if (self) {
    }
    return self;
}

- (void)loadView
{
    [super loadView];
    m_Font = [NSFont fontWithStringDescription:[NSString stringWithUTF8StdString:GlobalConfig().GetString(g_ConfigFont).value_or("")]];
    if(!m_Font) m_Font = [NSFont fontWithName:@"Menlo-Regular" size:13];

    [self updateFontVisibleName];
    [self.view layoutSubtreeIfNeeded];
}

-(NSString*)identifier{
    return NSStringFromClass(self.class);
}
-(NSImage*)toolbarItemImage{
    return [NSImage imageNamed:@"pref_term_icon"];
}
-(NSString*)toolbarItemLabel{
    return NSLocalizedStringFromTable(@"Terminal",
                                      @"Preferences",
                                      "General preferences tab title");
}

- (void) updateFontVisibleName
{
    self.fontVisibleName.stringValue = [NSString stringWithFormat:@"%@ %.0f pt.", m_Font.displayName, m_Font.pointSize];
}

- (IBAction)OnSetFont:(id)sender
{
    NSFontManager * fontManager = [NSFontManager sharedFontManager];
    fontManager.target = self;
    fontManager.action = @selector(changeFont:);
    [fontManager setSelectedFont:m_Font isMultiple:NO];
    [fontManager orderFrontFontPanel:self];
}

- (void)changeFont:(id)sender
{
    m_Font = [sender convertFont:m_Font];
    GlobalConfig().Set(g_ConfigFont, m_Font.toStringDescription.UTF8String);
    [self updateFontVisibleName];
}

@end
