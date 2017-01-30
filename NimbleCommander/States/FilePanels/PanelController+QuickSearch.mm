//
//  PanelController+QuickSearch.m
//  Files
//
//  Created by Michael G. Kazakov on 25.01.14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#include "PanelController+QuickSearch.h"

static const nanoseconds g_FastSeachDelayTresh = 4s;

static bool IsQuickSearchModifier(NSUInteger _modif, PanelQuickSearchMode::KeyModif _mode)
{
    // exclude CapsLock from our decision process
    _modif &= ~NSAlphaShiftKeyMask;
    
    switch (_mode) {
        case PanelQuickSearchMode::WithAlt:
            return (_modif&NSDeviceIndependentModifierFlagsMask) == NSAlternateKeyMask ||
            (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSShiftKeyMask);
        case PanelQuickSearchMode::WithCtrlAlt:
            return (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSControlKeyMask) ||
            (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSControlKeyMask|NSShiftKeyMask);
        case PanelQuickSearchMode::WithShiftAlt:
            return (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSShiftKeyMask);
        case PanelQuickSearchMode::WithoutModif:
            return (_modif&NSDeviceIndependentModifierFlagsMask) == 0 ||
            (_modif&NSDeviceIndependentModifierFlagsMask) == NSShiftKeyMask ;
        default:
            break;
    }
    return false;
}

static bool IsQuickSearchModifierForArrows(NSUInteger _modif, PanelQuickSearchMode::KeyModif _mode)
{
    // exclude CapsLock from our decision process
    _modif &= ~NSAlphaShiftKeyMask;
    
    // arrow keydowns have NSNumericPadKeyMask and NSFunctionKeyMask flag raised
    if((_modif & NSNumericPadKeyMask) == 0) return false;
    if((_modif & NSFunctionKeyMask) == 0) return false;
    _modif &= ~NSNumericPadKeyMask;
    _modif &= ~NSFunctionKeyMask;
    
    switch (_mode) {
        case PanelQuickSearchMode::WithAlt:
            return (_modif&NSDeviceIndependentModifierFlagsMask) == NSAlternateKeyMask ||
            (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSShiftKeyMask);
        case PanelQuickSearchMode::WithCtrlAlt:
            return (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSControlKeyMask) ||
            (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSControlKeyMask|NSShiftKeyMask);
        case PanelQuickSearchMode::WithShiftAlt:
            return (_modif&NSDeviceIndependentModifierFlagsMask) == (NSAlternateKeyMask|NSShiftKeyMask);
        default:
            break;
    }
    return false;
}

static bool IsQuickSearchStringCharacter(NSString *_s)
{
    static NSCharacterSet *chars;
    static once_flag once;
    call_once(once, []{
        NSMutableCharacterSet *un = [NSMutableCharacterSet new];
        [un formUnionWithCharacterSet:[NSCharacterSet alphanumericCharacterSet]];
        [un formUnionWithCharacterSet:[NSCharacterSet punctuationCharacterSet]];
        [un formUnionWithCharacterSet:[NSCharacterSet symbolCharacterSet]];
        [un removeCharactersInString:@"/"]; // such character simply can't appear in filename under unix
        chars = un;
    });
    
    if(_s.length == 0)
        return false;
    
    unichar u = [_s characterAtIndex:0]; // consider uing UTF-32 here
    return [chars characterIsMember:u];
}

static inline bool IsBackspace(NSString *_s)
{
    return _s.length == 1 && [_s characterAtIndex:0] == 0x7F;
}

static inline bool IsSpace(NSString *_s)
{
    return _s.length == 1 && [_s characterAtIndex:0] == 0x20;
}

static NSString *RemoveLastCharacterWithNormalization(NSString *_s)
{
    // remove last symbol. since strings are decomposed (as for file system interaction),
    // it should be composed first and decomposed back after altering
    assert(_s != nil);
    assert(_s.length > 0);
    NSString *s = _s.precomposedStringWithCanonicalMapping;
    s = [s substringToIndex:s.length-1];
    return s.decomposedStringWithCanonicalMapping;
}

//static NSString *PromptForMatchesAndString(unsigned _matches, NSString *_string)
//{
//    if(_matches == 0)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"Not found | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 1)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"1 match | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 2)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"2 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 3)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"3 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 4)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"4 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 5)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"5 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 6)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"6 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 7)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"7 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 8)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"8 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else if(_matches == 9)
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"9 matches | %@", @"FilePanelsQuickSearch", ""), _string];
//    else
//        return [NSString stringWithFormat:NSLocalizedStringFromTable(@"%1$@ matches | %2$@", @"FilePanelsQuickSearch", ""), [NSNumber numberWithInt:_matches], _string];
//}

NSString *ModifyStringByKeyDownString(NSString *_str, NSString *_key)
{
    if( !_key )
        return _str;
    
    if( !IsBackspace(_key) )
        _str = _str ? [_str stringByAppendingString:_key] : _key;
    else
        _str = _str.length > 0 ? RemoveLastCharacterWithNormalization(_str) : nil;
    
    return _str;
}


@implementation PanelController (QuickSearch)


- (void) QuickSearchClearFiltering
{
    if(m_View == nil)
        return;
    
    panel::GenericCursorPersistance pers(m_View, m_Data);
    
    bool any_changed = m_Data.ClearTextFiltering();
    
    [m_View setQuickSearchPrompt:nil withMatchesCount:0];
    [m_View dataUpdated]; // need to call only when something actually changed
    
    if( any_changed )
        pers.Restore();
}

- (bool)HandleQuickSearchSoft: (NSString*) _key
{
    nanoseconds currenttime = machtime();

    // update soft filtering
    NSString *text = m_Data.SoftFiltering().text;
    if( m_QuickSearchLastType + g_FastSeachDelayTresh < currenttime )
        text = nil;
    
    text = ModifyStringByKeyDownString(text, _key);
    if( !text  )
        return false;
    
    if( text.length == 0 ) {
        [self QuickSearchClearFiltering];
        return true;
    }
    
    [self SetQuickSearchSoft:text];

    return true;
}

- (void)SetQuickSearchSoft:(NSString*) _text
{
    if( !_text )
        return;
    
    nanoseconds currenttime = machtime();
    
    // update soft filtering
    auto filtering = m_Data.SoftFiltering();
    if( m_QuickSearchLastType + g_FastSeachDelayTresh < currenttime )
        m_QuickSearchOffset = 0;
    
    filtering.text = _text;
    filtering.type = m_QuickSearchWhere;
    filtering.ignoredotdot = false;
    m_Data.SetSoftFiltering(filtering);
    
    m_QuickSearchLastType = currenttime;
    
    if( !m_Data.EntriesBySoftFiltering().empty() ) {
        if(m_QuickSearchOffset >= m_Data.EntriesBySoftFiltering().size())
            m_QuickSearchOffset = (unsigned)m_Data.EntriesBySoftFiltering().size() - 1;
        m_View.curpos = m_Data.EntriesBySoftFiltering()[m_QuickSearchOffset];
    }
    
    if( m_QuickSearchTypingView ) {
        int total = (int)m_Data.EntriesBySoftFiltering().size();
        [m_View setQuickSearchPrompt:m_Data.SoftFiltering().text withMatchesCount:total];
        //        m_View.quickSearchPrompt = PromptForMatchesAndString(total, m_Data.SoftFiltering().text);
        
        // automatically remove prompt after g_FastSeachDelayTresh
        __weak PanelController *wself = self;
        dispatch_to_main_queue_after(g_FastSeachDelayTresh + 1000ns, [=]{
            if(PanelController *sself = wself)
                if(sself->m_QuickSearchLastType + g_FastSeachDelayTresh <= machtime()) {
                    [sself->m_View setQuickSearchPrompt:nil withMatchesCount:0];
                    //                    sself->m_View.quickSearchPrompt = nil;
                }
        });
    }
    
    [m_View volatileDataChanged];
}

- (void)QuickSearchHardUpdateTypingUI
{
    if(!m_QuickSearchTypingView)
        return;
    
    auto filtering = m_Data.HardFiltering();
    if(!filtering.text.text) {
        [m_View setQuickSearchPrompt:nil withMatchesCount:0];
//        m_View.quickSearchPrompt = nil;
    }
    else {
        int total = (int)m_Data.SortedDirectoryEntries().size();
        if(total > 0 && m_Data.Listing().IsDotDot(0))
            total--;
//        m_View.quickSearchPrompt = PromptForMatchesAndString(total, filtering.text.text);
        [m_View setQuickSearchPrompt:filtering.text.text withMatchesCount:total];
    }
}

- (bool)HandleQuickSearchHard:(NSString*) _key
{
    NSString *text = m_Data.HardFiltering().text.text;
    
    text = ModifyStringByKeyDownString(text, _key);
    if( text == nil )
        return false;

    if( text.length == 0 ) {
        [self QuickSearchClearFiltering];
        return true;
    }

    [self SetQuickSearchHard:text];
    
    return true;
}

- (void)SetQuickSearchHard:(NSString*)_text
{
    auto filtering = m_Data.HardFiltering();
    filtering.text.text = _text;
    if( filtering.text.text == nil )
        return;
    
    panel::GenericCursorPersistance pers(m_View, m_Data);
    
    filtering.text.type = m_QuickSearchWhere;
    filtering.text.clearonnewlisting = true;
    m_Data.SetHardFiltering(filtering);
    
    pers.Restore();
    
    [m_View dataUpdated];
    [self QuickSearchHardUpdateTypingUI];
    
    // for convinience - if we have ".." and cursor is on it - move it to first element (if any)
    if((m_VFSFetchingFlags & VFSFlags::F_NoDotDot) == 0 &&
       m_View.curpos == 0 &&
       m_Data.SortedDirectoryEntries().size() >= 2 &&
       m_Data.EntryAtRawPosition(m_Data.SortedDirectoryEntries()[0]).IsDotDot() )
        m_View.curpos = 1;
    
}

- (void) QuickSearchSetCriteria:(NSString *)_text
{
    if( m_QuickSearchIsSoftFiltering )
        [self SetQuickSearchSoft:_text];
    else
        [self SetQuickSearchHard:_text];
}

- (void)QuickSearchPrevious
{
    if(m_QuickSearchOffset > 0)
        m_QuickSearchOffset--;
    [self HandleQuickSearchSoft:nil];
}

- (void)QuickSearchNext
{
    m_QuickSearchOffset++;
    [self HandleQuickSearchSoft:nil];
}

- (bool) QuickSearchProcessKeyDown:(NSEvent *)event
{
    NSString*  const character   = [event charactersIgnoringModifiers];
    NSUInteger const modif       = [event modifierFlags];
    
    bool empty_text = m_QuickSearchIsSoftFiltering ?
        m_Data.SoftFiltering().text.length == 0 :
        m_Data.HardFiltering().text.text.length == 0;
    
    if( IsQuickSearchModifier(modif, m_QuickSearchMode) &&
        ( IsQuickSearchStringCharacter(character) ||
            ( !empty_text && IsSpace(character) ) ||
            IsBackspace(character)
         )
       ) {
        if(m_QuickSearchIsSoftFiltering)
            return [self HandleQuickSearchSoft:character.decomposedStringWithCanonicalMapping];
        else
            return [self HandleQuickSearchHard:character.decomposedStringWithCanonicalMapping];
    }
    else if( character.length == 1 )
        switch([character characterAtIndex:0]) {
            case NSUpArrowFunctionKey:
                if( IsQuickSearchModifierForArrows(modif, m_QuickSearchMode) ) {
                    [self QuickSearchPrevious];
                    return true;
                }
            case NSDownArrowFunctionKey:
                if( IsQuickSearchModifierForArrows(modif, m_QuickSearchMode) ) {
                    [self QuickSearchNext];
                    return true;
                }
        }
    
    return false;
}

- (void) QuickSearchUpdate
{
    if(!m_QuickSearchIsSoftFiltering)
        [self QuickSearchHardUpdateTypingUI];
}

@end