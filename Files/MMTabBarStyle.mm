#import "MMTabBarStyle.h"
#import "3rd_party/MMTabBarView/MMTabBarView/MMAttachedTabBarButton.h"
#import "3rd_party/MMTabBarView/MMTabBarView/NSView+MMTabBarViewExtensions.h"

static CGColorRef DividerColor(bool _wnd_active)
{
    static CGColorRef act = CGColorCreateGenericRGB(176/255.0, 176/255.0, 176/255.0, 1.0);
    static CGColorRef inact = CGColorCreateGenericRGB(225/255.0, 225/255.0, 225/255.0, 1.0);
    return _wnd_active ? act : inact;
}

@implementation MMTabBarStyle
{
    NSImage *cardCloseButton;
    NSImage *cardCloseButtonDown;
    NSImage *cardCloseButtonOver;
    NSImage *cardCloseDirtyButton;
    NSImage *cardCloseDirtyButtonDown;
    NSImage *cardCloseDirtyButtonOver;
}

+ (NSString *)name {
    return @"Files";
}

- (NSString *)name {
	return self.class.name;
}

- (id) init {
    if ( (self = [super init]) ) {
        cardCloseButton = [[NSImage alloc] initByReferencingFile:[[MMTabBarView bundle] pathForImageResource:@"AquaTabClose_Front"]];
        cardCloseButtonDown = [[NSImage alloc] initByReferencingFile:[[MMTabBarView bundle] pathForImageResource:@"AquaTabClose_Front_Pressed"]];
        cardCloseButtonOver = [[NSImage alloc] initByReferencingFile:[[MMTabBarView bundle] pathForImageResource:@"AquaTabClose_Front_Rollover"]];
        
        cardCloseDirtyButton = [[NSImage alloc] initByReferencingFile:[[MMTabBarView bundle] pathForImageResource:@"AquaTabCloseDirty_Front"]];
        cardCloseDirtyButtonDown = [[NSImage alloc] initByReferencingFile:[[MMTabBarView bundle] pathForImageResource:@"AquaTabCloseDirty_Front_Pressed"]];
        cardCloseDirtyButtonOver = [[NSImage alloc] initByReferencingFile:[[MMTabBarView bundle] pathForImageResource:@"AquaTabCloseDirty_Front_Rollover"]];
	}
    return self;
}

- (CGFloat)leftMarginForTabBarView:(MMTabBarView *)tabBarView
{
    return 0.0;
}

- (CGFloat)rightMarginForTabBarView:(MMTabBarView *)tabBarView
{
    return 0.0;
}

- (CGFloat)topMarginForTabBarView:(MMTabBarView *)tabBarView
{
    return 0.0;
}

- (CGFloat)bottomMarginForTabBarView:(MMTabBarView *)tabBarView
{
    return 0.0;
}

- (CGFloat)heightOfTabBarButtonsForTabBarView:(MMTabBarView *)tabBarView
{

    return kMMTabBarViewHeight;
}

- (BOOL)supportsOrientation:(MMTabBarOrientation)orientation forTabBarView:(MMTabBarView *)tabBarView
{
    return orientation == MMTabBarHorizontalOrientation;
}

- (NSRect)draggingRectForTabButton:(MMAttachedTabBarButton *)aButton ofTabBarView:(MMTabBarView *)tabBarView {

	NSRect dragRect = [aButton stackingFrame];
	dragRect.size.width++;
	return dragRect;
}

- (NSImage *)closeButtonImageOfType:(MMCloseButtonImageType)type forTabCell:(MMTabBarButtonCell *)cell
{
    switch (type) {
        case MMCloseButtonImageTypeStandard:
            return cardCloseButton;
        case MMCloseButtonImageTypeRollover:
            return cardCloseButtonOver;
        case MMCloseButtonImageTypePressed:
            return cardCloseButtonDown;
            
        case MMCloseButtonImageTypeDirty:
            return cardCloseDirtyButton;
        case MMCloseButtonImageTypeDirtyRollover:
            return cardCloseDirtyButtonOver;
        case MMCloseButtonImageTypeDirtyPressed:
            return cardCloseDirtyButtonDown;
            
        default:
            break;
    }
}

- (void)drawBezelOfTabBarView:(MMTabBarView *)tabBarView inRect:(NSRect)rect {
    NSDrawWindowBackground(rect);
    
    CGContextRef context = (CGContextRef)NSGraphicsContext.currentContext.graphicsPort;

    // draw horizontal divider line
    CGContextSaveGState(context);
    CGContextSetStrokeColorWithColor(context, DividerColor(tabBarView.isWindowActive));
    NSPoint points[2] = { {rect.origin.x, rect.origin.y + rect.size.height - 0.5},
                          {rect.origin.x + rect.size.width, rect.origin.y + rect.size.height - 0.5} };
    CGContextStrokeLineSegments(context, points, 2);
    CGContextRestoreGState(context);
}

- (void)drawBezelOfTabCell:(MMTabBarButtonCell *)cell withFrame:(NSRect)frame inView:(NSView *)controlView
{
    frame.size.height -= 1; // for horizontal divider drawn by drawBezelOfTabBarView
    
    MMTabBarView *tabBarView = [controlView enclosingTabBarView];
    MMAttachedTabBarButton *button = (MMAttachedTabBarButton *)controlView;
    bool wnd_active = [tabBarView isWindowActive];
    bool tab_selected = [button state] == NSOnState;
    bool tab_isfr = tab_selected && tabBarView.tabView.selectedTabViewItem.view == tabBarView.window.firstResponder;
    bool button_hovered = [button mouseHovered];
    CGContextRef context = (CGContextRef)NSGraphicsContext.currentContext.graphicsPort;
    CGContextSaveGState(context);
    if(tab_selected) {
        if(wnd_active) {
            if(tab_isfr) {
                static CGColorRef bg = CGColorCreateGenericRGB(256./256., 256./256., 256./256., 1);
                CGContextSetFillColorWithColor(context, bg);
                CGContextFillRect(context, frame);
            }
            else
                NSDrawWindowBackground(frame);
        }
        else
            NSDrawWindowBackground(frame);
    }
    else {
        if(wnd_active) {
            if(!button_hovered) {
                static CGColorRef bg = CGColorCreateGenericRGB(180./256., 180./256., 180./256., 1);
                CGContextSetFillColorWithColor(context, bg);
                CGContextFillRect(context, frame);
            }
            else {
                static CGColorRef bg = CGColorCreateGenericRGB(160./256., 160./256., 160./256., 1);
                CGContextSetFillColorWithColor(context, bg);
                CGContextFillRect(context, frame);
            }
        }
        else
            NSDrawWindowBackground(frame);
    }

    // draw vertical divider
    CGContextSetStrokeColorWithColor(context, DividerColor(wnd_active));
    NSPoint points[2] = { {frame.origin.x + frame.size.width - 0.5, frame.origin.y} ,
        {frame.origin.x + frame.size.width - 0.5, frame.origin.y + frame.size.height} };
    CGContextStrokeLineSegments(context, points, 2);
    CGContextRestoreGState(context);
}

- (void)drawTitleOfTabCell:(MMTabBarButtonCell *)cell withFrame:(NSRect)frame inView:(NSView *)controlView
{
    CGContextRef context = (CGContextRef)NSGraphicsContext.currentContext.graphicsPort;
    CGContextSaveGState(context);
    CGContextSetShouldSmoothFonts((CGContextRef)NSGraphicsContext.currentContext.graphicsPort, false);

    // draw title
    [[cell attributedStringValue] drawInRect:[cell titleRectForBounds:frame]];
    
   CGContextRestoreGState(context);
}

@end
