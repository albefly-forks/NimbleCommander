//
//  TermView.m
//  TermPlays
//
//  Created by Michael G. Kazakov on 17.11.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#include "../../OrthodoxMonospace.h"
#include "../../FontCache.h"
#include "../../Common.h"
#include "../../BlinkingCaret.h"
#include "../../HexadecimalColor.h"
#include "../../Config.h"
#include "TermView.h"
#include "TermScreen.h"
#include "TermParser.h"

static const auto g_ConfigMaxFPS = "terminal.maxFPS";
static const auto g_ConfigCursorMode = "terminal.cursorMode";
static const auto g_ConfigFont = "terminal.font";
static const auto g_ConfigForegroundColor       = "terminal.textColor";
static const auto g_ConfigBoldForegroundColor   = "terminal.boldTextColor";
static const auto g_ConfigBackgroundColor       = "terminal.backgroundColor";
static const auto g_ConfigSelectionColor        = "terminal.selectionColor";
static const auto g_ConfigCursorColor           = "terminal.cursorColor";
static const auto g_ConfigAnsi0  = "terminal.AnsiColor0";
static const auto g_ConfigAnsi1  = "terminal.AnsiColor1";
static const auto g_ConfigAnsi2  = "terminal.AnsiColor2";
static const auto g_ConfigAnsi3  = "terminal.AnsiColor3";
static const auto g_ConfigAnsi4  = "terminal.AnsiColor4";
static const auto g_ConfigAnsi5  = "terminal.AnsiColor5";
static const auto g_ConfigAnsi6  = "terminal.AnsiColor6";
static const auto g_ConfigAnsi7  = "terminal.AnsiColor7";
static const auto g_ConfigAnsi8  = "terminal.AnsiColor8";
static const auto g_ConfigAnsi9  = "terminal.AnsiColor9";
static const auto g_ConfigAnsi10 = "terminal.AnsiColor10";
static const auto g_ConfigAnsi11 = "terminal.AnsiColor11";
static const auto g_ConfigAnsi12 = "terminal.AnsiColor12";
static const auto g_ConfigAnsi13 = "terminal.AnsiColor13";
static const auto g_ConfigAnsi14 = "terminal.AnsiColor14";
static const auto g_ConfigAnsi15 = "terminal.AnsiColor15";

struct SelPoint
{
    int x;
    int y;
    inline bool operator > (const SelPoint&_r) const { return (y > _r.y) || (y == _r.y && x >  _r.x); }
    inline bool operator >=(const SelPoint&_r) const { return (y > _r.y) || (y == _r.y && x >= _r.x); }
    inline bool operator < (const SelPoint&_r) const { return !(*this >= _r); }
    inline bool operator <=(const SelPoint&_r) const { return !(*this >  _r); }
    inline bool operator ==(const SelPoint&_r) const { return y == _r.y && x == _r.x; }
    inline bool operator !=(const SelPoint&_r) const { return y != _r.y || x != _r.x; }
};

static uint32_t ConfigColor(const char *_path)
{
    return HexadecimalColorStringToRGBA(GlobalConfig().GetString(_path).value_or(""));
}

struct AnsiColors : array<DoubleColor, 16>
{
    AnsiColors() : array{{
        ConfigColor(g_ConfigAnsi0), // Black
        ConfigColor(g_ConfigAnsi1), // Red
        ConfigColor(g_ConfigAnsi2), // Green
        ConfigColor(g_ConfigAnsi3), // Yellow
        ConfigColor(g_ConfigAnsi4), // Blue
        ConfigColor(g_ConfigAnsi5), // Magenta
        ConfigColor(g_ConfigAnsi6), // Cyan
        ConfigColor(g_ConfigAnsi7), // White
        ConfigColor(g_ConfigAnsi8), // Bright Black
        ConfigColor(g_ConfigAnsi9), // Bright Red
        ConfigColor(g_ConfigAnsi10),// Bright Green
        ConfigColor(g_ConfigAnsi11),// Bright Yellow
        ConfigColor(g_ConfigAnsi12),// Bright Blue
        ConfigColor(g_ConfigAnsi13),// Bright Magenta
        ConfigColor(g_ConfigAnsi14),// Bright Cyan
        ConfigColor(g_ConfigAnsi15) // Bright White
    }}{}
};

static inline bool IsBoxDrawingCharacter(uint32_t _ch)
{
    return _ch >= 0x2500 && _ch <= 0x257F;
}

@implementation TermView
{
    shared_ptr<FontCache> m_FontCache;
    TermScreen     *m_Screen;
    TermParser     *m_Parser;
    
    int             m_LastScreenFullHeight;
    
    bool            m_HasSelection;
    bool            m_ReportsSizeByOccupiedContent;
    SelPoint        m_SelStart;
    SelPoint        m_SelEnd;
    AnsiColors      m_AnsiColors;
    DoubleColor     m_ForegroundColor;
    DoubleColor     m_BoldForegroundColor;
    DoubleColor     m_BackgroundColor;
    DoubleColor     m_SelectionColor;
    DoubleColor     m_CursorColor;
    TermViewCursor  m_CursorType;
    FPSLimitedDrawer *m_FPS;
    NSSize          m_IntrinsicSize;
    unique_ptr<BlinkingCaret> m_BlinkingCaret;
    vector<GenericConfig::ObservationTicket> m_ConfigObservationTickets;
}

@synthesize fpsDrawer = m_FPS;
@synthesize reportsSizeByOccupiedContent = m_ReportsSizeByOccupiedContent;

- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        m_BlinkingCaret = make_unique<BlinkingCaret>(self);
        m_LastScreenFullHeight = 0;
        m_HasSelection = false;
        m_ReportsSizeByOccupiedContent = false;
        m_FPS = [[FPSLimitedDrawer alloc] initWithView:self];
        m_FPS.fps = GlobalConfig().GetInt(g_ConfigMaxFPS);
        m_IntrinsicSize = NSMakeSize(NSViewNoInstrinsicMetric, frame.size.height);
        [self reloadGeometry];
        [self reloadAppearance];

        __weak TermView* weak_self = self;
        GlobalConfig().ObserveMany(m_ConfigObservationTickets,
                                   [=]{ [(TermView*)weak_self reloadAppearance]; },
                                   initializer_list<const char*>{
                                       g_ConfigCursorMode,
                                       g_ConfigForegroundColor,
                                       g_ConfigBoldForegroundColor,
                                       g_ConfigBackgroundColor,
                                       g_ConfigSelectionColor,
                                       g_ConfigCursorColor,
                                       g_ConfigAnsi0,
                                       g_ConfigAnsi1,
                                       g_ConfigAnsi2,
                                       g_ConfigAnsi3,
                                       g_ConfigAnsi4,
                                       g_ConfigAnsi5,
                                       g_ConfigAnsi6,
                                       g_ConfigAnsi7,
                                       g_ConfigAnsi8,
                                       g_ConfigAnsi9,
                                       g_ConfigAnsi10,
                                       g_ConfigAnsi11,
                                       g_ConfigAnsi12,
                                       g_ConfigAnsi13,
                                       g_ConfigAnsi14,
                                       g_ConfigAnsi15
                                   });
    }
    return self;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)becomeFirstResponder
{
    self.needsDisplay = true;
    return YES;
}

- (BOOL)resignFirstResponder
{
    self.needsDisplay = true;
    return YES;
}

-(BOOL) isOpaque
{
	return YES;
}

- (void)resetCursorRects
{
    [self addCursorRect:self.frame cursor:[NSCursor IBeamCursor]];
}

- (TermParser *)parser
{
    return m_Parser;
}

- (const FontCache&) fontCache
{
    return *m_FontCache;
}

- (void) AttachToScreen:(TermScreen*)_scr
{
    m_Screen = _scr;
}

- (void) AttachToParser:(TermParser*)_par
{
    m_Parser = _par;
}

- (void) setAllowCursorBlinking:(bool)allowCursorBlinking
{
    m_BlinkingCaret->SetEnabled(allowCursorBlinking);
}

- (bool) allowCursorBlinking
{
    return m_BlinkingCaret->Enabled();
}

- (void) reloadAppearance
{
    m_ForegroundColor       = ConfigColor(g_ConfigForegroundColor);
    m_BoldForegroundColor   = ConfigColor(g_ConfigBoldForegroundColor);
    m_BackgroundColor       = ConfigColor(g_ConfigBackgroundColor);
    m_SelectionColor        = ConfigColor(g_ConfigSelectionColor);
    m_CursorColor           = ConfigColor(g_ConfigCursorColor);
    m_CursorType            = (TermViewCursor)GlobalConfig().GetInt(g_ConfigCursorMode);
    m_AnsiColors            = AnsiColors();
    [self setNeedsDisplay:true];
}

- (void) reloadGeometry
{
    NSFont *font = [NSFont fontWithStringDescription:[NSString stringWithUTF8StdString:GlobalConfig().GetString(g_ConfigFont).value_or("")]];
    if(!font)
        font = [NSFont fontWithName:@"Menlo-Regular" size:13];
    m_FontCache             = FontCache::FontCacheFromFont((__bridge CTFontRef)font);
}

- (void)keyDown:(NSEvent *)event
{
    NSString*  const character = [event charactersIgnoringModifiers];
    if ( [character length] == 1 )
        m_HasSelection = false;

    m_Parser->ProcessKeyDown(event);
    [self scrollToBottom];
}

- (NSSize) intrinsicContentSize
{
    return m_IntrinsicSize;
}

- (int)fullScreenHeight
{
    if( !m_ReportsSizeByOccupiedContent ) {
        return m_Screen->Height() + m_Screen->Buffer().BackScreenLines();
    }
    else {
        int onscreen = 0;
        if( auto occupied = m_Screen->Buffer().OccupiedOnScreenLines() )
            onscreen = occupied->second;
        if( m_Screen->CursorY() >= onscreen )
            onscreen = m_Screen->CursorY() + 1;
        return m_Screen->Buffer().BackScreenLines() + onscreen;
    }
}

- (void)adjustSizes:(bool)_mandatory
{
//    int full_height = m_Screen->Height() + m_Screen->Buffer().BackScreenLines();
    int full_height = self.fullScreenHeight;
    if(full_height == m_LastScreenFullHeight && _mandatory == false)
        return;
    
    m_LastScreenFullHeight = full_height;
    
    auto clipview = self.enclosingScrollView.contentView;
    double sy = full_height * m_FontCache->Height();
    double rest = clipview.frame.size.height -
        floor(clipview.frame.size.height / m_FontCache->Height()) * m_FontCache->Height();

    m_IntrinsicSize = NSMakeSize(NSViewNoInstrinsicMetric, sy + rest);
//    NSLog(@"height = %f, addition = %f", m_IntrinsicSize.height, rest);
    [self invalidateIntrinsicContentSize];
    [self.enclosingScrollView layoutSubtreeIfNeeded];
    
    
    [self scrollToBottom];
}

- (void) scrollToBottom
{
    auto scrollview = self.enclosingScrollView;
    auto clipview = scrollview.contentView;
    
    auto h1 = self.frame.size.height;
    auto h2 = scrollview.contentSize.height;
    if( h1 > h2 ) {
        auto p = NSMakePoint(0,
                             self.superview.isFlipped ?
                                (self.frame.size.height - scrollview.contentSize.height) :
                                0
                             );
        [clipview scrollToPoint:p];
        [scrollview reflectScrolledClipView:clipview];
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
	[super drawRect:dirtyRect];
	
    // Drawing code here.
    CGContextRef context = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];
    oms::SetFillColor(context, m_BackgroundColor);
    CGContextFillRect(context, NSRectToCGRect(self.bounds));
    
    if(!m_Screen)
        return;
    
/*    static uint64_t last_redraw = GetTimeInNanoseconds();
    uint64_t now = GetTimeInNanoseconds();
    NSLog(@"%llu", (now - last_redraw)/1000000);
    last_redraw = now;*/
    
//    MachTimeBenchmark mtb;

    int line_start=0, line_end=0;
    if( self.superview.isFlipped ) {
        auto clipviewbounds = self.enclosingScrollView.contentView.bounds;
        line_start = floor(clipviewbounds.origin.y / m_FontCache->Height());
        line_end   = line_start + ceil(clipviewbounds.size.height / m_FontCache->Height());
    }
    else {
        auto clipviewbounds = self.enclosingScrollView.contentView.bounds;
        line_end = ceil( (self.bounds.size.height - clipviewbounds.origin.y) / m_FontCache->Height());
        line_start = line_end - ceil(clipviewbounds.size.height / m_FontCache->Height());
    }
    
    
    m_Screen->Lock();
    
//    oms::SetParamsForUserASCIIArt(context, m_FontCache);
    oms::SetParamsForUserReadableText(context, m_FontCache.get());
    CGContextSetShouldSmoothFonts(context, true);

    for(int i = line_start, bsl = m_Screen->Buffer().BackScreenLines();
        i < line_end;
        ++i)
    {
        if(i < bsl) { // scrollback
            if(auto line = m_Screen->Buffer().LineFromNo(i - bsl))
                [self DrawLine:line
                          at_y:i
                         sel_y:i - bsl
                       context:context
                     cursor_at:-1];
        }
        else { // real screen
            if(auto line = m_Screen->Buffer().LineFromNo(i - bsl))
                [self DrawLine:line
                          at_y:i
                         sel_y:i - bsl
                       context:context
                     cursor_at:(m_Screen->CursorY() != i - bsl) ? -1 : m_Screen->CursorX()];
        }
    }
    
#ifdef DEBUG
//    [self drawBackscreenOnscreenBorder:context];
#endif
    
    m_Screen->Unlock();
    
//    mtb.ResetMilli();
}

- (void)drawBackscreenOnscreenBorder:(CGContextRef)_context
{
    CGRect rc;
    rc.origin.x = 0;
    rc.origin.y = m_Screen->Buffer().BackScreenLines() * m_FontCache->Height();
    rc.size.width = self.bounds.size.width;
    rc.size.height = 1;
    CGContextSetRGBFillColor(_context, 1, 1, 1, 1);
    CGContextFillRect(_context, rc);
}

- (void) DrawLine:(TermScreenBuffer::RangePair<const TermScreenBuffer::Space>)_line
             at_y:(int)_y
            sel_y:(int)_sel_y
          context:(CGContextRef)_context
        cursor_at:(int)_cur_x
{
    // draw backgrounds
    DoubleColor curr_c = {-1, -1, -1, -1};
    int x = 0;
//    for(TermScreen::Space char_space: _line.chars)
    for(auto char_space: _line)
    {
        int bg_no = char_space.reverse ? char_space.foreground : char_space.background;
        if(bg_no != TermScreenColors::Default) {
            const DoubleColor &c = m_AnsiColors[bg_no];
            if(c != m_BackgroundColor) {
                if(c != curr_c)
                    oms::SetFillColor(_context, curr_c = c);
        
                CGContextFillRect(_context,
                                  CGRectMake(x * m_FontCache->Width(),
                                             _y * m_FontCache->Height(),
                                             m_FontCache->Width(),
                                             m_FontCache->Height()));
            }
        }
        ++x;
    }
    
    // draw selection if it's here
    if(m_HasSelection)
    {
        CGRect rc = {{-1, -1}, {0, 0}};
        if(m_SelStart.y == m_SelEnd.y && m_SelStart.y == _sel_y)
            rc = CGRectMake(m_SelStart.x * m_FontCache->Width(),
                            _y * m_FontCache->Height(),
                            (m_SelEnd.x - m_SelStart.x) * m_FontCache->Width(),
                            m_FontCache->Height());
        else if(_sel_y < m_SelEnd.y && _sel_y > m_SelStart.y)
            rc = CGRectMake(0,
                            _y * m_FontCache->Height(),
                            self.frame.size.width,
                            m_FontCache->Height());
        else if(_sel_y == m_SelStart.y)
            rc = CGRectMake(m_SelStart.x * m_FontCache->Width(),
                            _y * m_FontCache->Height(),
                            self.frame.size.width - m_SelStart.x * m_FontCache->Width(),
                            m_FontCache->Height());
        else if(_sel_y == m_SelEnd.y)
            rc = CGRectMake(0,
                            _y * m_FontCache->Height(),
                            m_SelEnd.x * m_FontCache->Width(),
                            m_FontCache->Height());
        
        if(rc.origin.x >= 0)
        {
            oms::SetFillColor(_context, m_SelectionColor);
            CGContextFillRect(_context, rc);
        }
        
    }
    
    // draw cursor if it's here
    if(_cur_x >= 0)
        [self drawCursor:NSMakeRect(_cur_x * m_FontCache->Width(),
                                   _y * m_FontCache->Height(),
                                   m_FontCache->Width(),
                                   m_FontCache->Height())
                 context:_context];
    
    // draw glyphs
    x = 0;
    curr_c = {-1, -1, -1, -1};
    CGContextSetShouldAntialias(_context, true);
    
//    for(TermScreen::Space char_space: _line.chars)
    for(auto char_space: _line)
    {
        DoubleColor c = m_ForegroundColor;
        if(char_space.reverse) {
            c = char_space.background != TermScreenColors::Default ?
                m_AnsiColors[char_space.background] :
                m_BackgroundColor;
        } else {
            int foreground = char_space.foreground;
            if(foreground != TermScreenColors::Default){
                if(char_space.intensity)
                    foreground += 8;
                c = m_AnsiColors[foreground];
            } else {
                if(char_space.intensity)
                    c = m_BoldForegroundColor;
            }
        }
        
        if(char_space.l != 0 &&
           char_space.l != 32 &&
           char_space.l != TermScreen::MultiCellGlyph
           )
        {
            if(c != curr_c)
                oms::SetFillColor(_context, curr_c = c);
            
            bool pop = false;
            if( IsBoxDrawingCharacter(char_space.l) ) {
                CGContextSaveGState(_context);
                CGContextSetShouldAntialias(_context, false);
                pop = true;
                
            }
            
            oms::DrawSingleUniCharXY(char_space.l, x, _y, _context, m_FontCache.get());
            
            if(char_space.c1 != 0)
                oms::DrawSingleUniCharXY(char_space.c1, x, _y, _context, m_FontCache.get());
            if(char_space.c2 != 0)
                oms::DrawSingleUniCharXY(char_space.c2, x, _y, _context, m_FontCache.get());
            
            if(pop)
                CGContextRestoreGState(_context);
        }        
        
        if(char_space.underline)
        {
            /* NEED REAL UNDERLINE POSITION HERE !!! */
            // need to set color here?
            CGRect rc;
            rc.origin.x = x * m_FontCache->Width();
            rc.origin.y = _y * m_FontCache->Height() + m_FontCache->Height() - 1;
            rc.size.width = m_FontCache->Width();
            rc.size.height = 1;
            CGContextFillRect(_context, rc);
        }
        
        ++x;
    }
}

- (void)drawCursor:(NSRect)_char_rect context:(CGContextRef)_context
{
    const bool is_wnd_active = NSView.focusView.window.isKeyWindow;
    const bool is_first_responder = self.window.firstResponder == self;
    
    if( is_wnd_active && is_first_responder ) {
        m_BlinkingCaret->ScheduleNextRedraw(); // be sure not to call Shedule... when view is not active
        if( m_BlinkingCaret->Visible() ) {
            oms::SetFillColor(_context, m_CursorColor);
            switch (m_CursorType) {
                case TermViewCursor::Block:
                    CGContextFillRect(_context, NSRectToCGRect(_char_rect));
                    break;
                    
                case TermViewCursor::Underline:
                    CGContextFillRect(_context,
                                      CGRectMake(_char_rect.origin.x,
                                                 _char_rect.origin.y + _char_rect.size.height - 2,
                                                 _char_rect.size.width,
                                                 2));
                    break;
                    
                case TermViewCursor::VerticalBar:
                    CGContextFillRect(_context,
                                      CGRectMake(_char_rect.origin.x, _char_rect.origin.y, 1., _char_rect.size.height)
                                      );
                    break;
            }
        }
    }
    else {
        oms::SetStrokeColor(_context, m_CursorColor);
        CGContextSetLineWidth(_context, 1);
        CGContextSetShouldAntialias(_context, false);
        _char_rect.origin.y += 1;
        _char_rect.size.height -= 1;
        CGContextStrokeRect(_context, NSRectToCGRect(_char_rect));
    }
}

- (NSRect)adjustScroll:(NSRect)proposedVisibleRect
{
    proposedVisibleRect.origin.y = (int)(proposedVisibleRect.origin.y/m_FontCache->Height() + 0.5) * m_FontCache->Height();
    return proposedVisibleRect;
}

/**
 * return predicted character position regarding current font setup
 * y values [0...+y should be treated as rows in real terminal screen
 * y values -y...0) should be treated as rows in backscroll. y=-1 mean the closes to real screen row
 * x values are trivial - float x position divided by font's width
 * returned points may not correlate with real lines' lengths or scroll sizes, so they need to be treated carefully
 */
- (SelPoint)ProjectPoint:(NSPoint)_point
{
    int line_predict = floor(_point.y / m_FontCache->Height()) - m_Screen->Buffer().BackScreenLines();
    int col_predict = floor(_point.x / m_FontCache->Width());
    return SelPoint{col_predict, line_predict};
}

- (void) mouseDown:(NSEvent *)_event
{

//    NSPoint pt = [m_View convertPoint:[event locationInWindow] fromView:nil];
//    [self ProjectPoint:[self convertPoint:[_event locationInWindow] fromView:nil]];
    [self HandleSelectionWithMouseDragging:_event];
}

- (void) HandleSelectionWithMouseDragging: (NSEvent*) event
{
    // TODO: not a precise selection modification. look at viewer, it has better implementation.
    
    bool modifying_existing_selection = ([event modifierFlags] & NSShiftKeyMask) ? true : false;
    NSPoint first_loc = [self convertPoint:[event locationInWindow] fromView:nil];
    
    while ([event type]!=NSLeftMouseUp)
    {
        NSPoint curr_loc = [self convertPoint:[event locationInWindow] fromView:nil];
        
        SelPoint start = [self ProjectPoint:first_loc];
        SelPoint end   = [self ProjectPoint:curr_loc];
        
        if(start > end)
            swap(start, end);
        
        
        if(modifying_existing_selection && m_HasSelection)
        {
            if(end > m_SelStart) {
                m_SelEnd = end;
                [self setNeedsDisplay];
            }
            else if(end < m_SelStart) {
                m_SelStart = end;
                [self setNeedsDisplay];
            }
        }
        else if(!m_HasSelection || m_SelEnd != end || m_SelStart != start)
        {
            m_HasSelection = true;
            m_SelStart = start;
            m_SelEnd = end;
            [self setNeedsDisplay];
        }

        event = [self.window nextEventMatchingMask:(NSLeftMouseDraggedMask | NSLeftMouseUpMask)];
    }
}

- (void)copy:(id)sender
{
    if(!m_HasSelection)
        return;
    
    if(m_SelStart == m_SelEnd)
        return;
    
    vector<uint32_t> unichars;
    SelPoint curr = m_SelStart;
    while(true)
    {
        if(curr >= m_SelEnd) break;
        
        auto line = m_Screen->Buffer().LineFromNo( curr.y );
        
        if( !line ) {
            curr.y++;
            continue;
        }
        
        bool any_inserted = false;
        auto chars_len = line.second - line.first;
        for(; curr.x < chars_len && ( (curr.y == m_SelEnd.y) ? (curr.x < m_SelEnd.x) : true); ++curr.x) {
            auto &sp = line.first[curr.x];
            if(sp.l == TermScreen::MultiCellGlyph) continue;
            unichars.push_back(sp.l != 0 ? sp.l : ' ');
            if(sp.c1 != 0) unichars.push_back(sp.c1);
            if(sp.c2 != 0) unichars.push_back(sp.c2);
            any_inserted = true;
        }
    
        if(curr >= m_SelEnd)
            break;
        
        if(any_inserted && !m_Screen->Buffer().LineWrapped( curr.y ))
            unichars.push_back(0x000A);
        
        curr.y++;
        curr.x = 0;
    }
    
    NSString *result = [[NSString alloc] initWithBytes:unichars.data()
                                                length:unichars.size() * sizeof(uint32_t)
                                              encoding:NSUTF32LittleEndianStringEncoding];
    NSPasteboard *pasteBoard = NSPasteboard.generalPasteboard;
    [pasteBoard clearContents];
    [pasteBoard declareTypes:@[NSStringPboardType] owner:nil];
    [pasteBoard setString:result forType:NSStringPboardType];
}

- (IBAction)paste:(id)sender
{    
    NSPasteboard *paste_board = [NSPasteboard generalPasteboard];
    NSString *best_type = [paste_board availableTypeFromArray:[NSArray arrayWithObject:NSStringPboardType]];
    if(!best_type)
        return;
    
    NSString *text = [paste_board stringForType:NSStringPboardType];
    if(!text)
        return;
    m_Parser->PushRawTaskInput(text);
}

- (void)selectAll:(id)sender
{
    m_HasSelection = true;
    m_SelStart.y = -m_Screen->Buffer().BackScreenLines();
    m_SelStart.x = 0;
    m_SelEnd.y = m_Screen->Height()-1;
    m_SelEnd.x = m_Screen->Width();
    [self setNeedsDisplay];
}

- (void)deselectAll:(id)sender
{
    m_HasSelection = false;
    [self setNeedsDisplay];
}

- (NSFont*) font
{
    return (__bridge NSFont*) m_FontCache->BaseFont();
}

- (NSColor*) ANSIColorForNo:(int)_number
{
    return (_number < 0 || _number >= m_AnsiColors.size()) ? nil : m_AnsiColors[_number].ToNSColor();
}

- (NSColor*) backgroundColor
{
    return m_BackgroundColor.ToNSColor();
}

@end