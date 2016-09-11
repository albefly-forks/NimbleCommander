#include "../../Core/FeedbackManager.h"
#include "AskingForRatingOverlayView.h"

@interface AskingForRatingOverlayLevelIndicator : NSLevelIndicator
@end

@implementation AskingForRatingOverlayLevelIndicator

- (void)updateTrackingAreas
{
    while( self.trackingAreas.count )
        [self removeTrackingArea:self.trackingAreas[0]];
    
    NSTrackingArea *trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                                options:NSTrackingMouseEnteredAndExited|NSTrackingMouseMoved|NSTrackingActiveInActiveApp
                                                                  owner:self
                                                               userInfo:nil
                                    ];
    [self addTrackingArea:trackingArea];
}

- (void)mouseMoved:(NSEvent *)event
{
    NSPoint mouseLocation = self.window.mouseLocationOutsideOfEventStream;
    mouseLocation = [self convertPoint:mouseLocation fromView: nil];
    
    float f = mouseLocation.x / self.bounds.size.width;
    int n = (int)floor( f * self.maxValue );
    self.integerValue = n+1;
}

- (void)mouseEntered:(NSEvent *)theEvent
{
    self.highlighted = true;
}

- (void)mouseExited:(NSEvent *)theEvent
{
    self.highlighted = false;
    self.integerValue = 5;
}

@end

@interface AskingForRatingOverlayView ()
@property bool mouseHover;
@end

@implementation AskingForRatingOverlayView
{
    AskingForRatingOverlayLevelIndicator    *m_LevelIndicator;
    NSTextField                             *m_Annotation;
    NSButton                                *m_DiscardButton;
    int                                     m_Rating;
}

- (instancetype) initWithFrame:(NSRect)frameRect
{
    self = [super initWithFrame:frameRect];
    if(self) {
        self.mouseHover = false;
        m_Rating = 0;
        
        m_LevelIndicator = [[AskingForRatingOverlayLevelIndicator alloc] initWithFrame:NSMakeRect(0, 0, 50, 20)];
        m_LevelIndicator.translatesAutoresizingMaskIntoConstraints = false;
        m_LevelIndicator.minValue = 0;
        m_LevelIndicator.maxValue = 5;
        m_LevelIndicator.levelIndicatorStyle = NSRatingLevelIndicatorStyle;
        m_LevelIndicator.integerValue = 5;
        m_LevelIndicator.enabled = true;
        m_LevelIndicator.highlighted = true;
        m_LevelIndicator.cell.editable = true;
        m_LevelIndicator.target = self;
        m_LevelIndicator.action = @selector(ratingClicked:);
        [self addSubview:m_LevelIndicator];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_LevelIndicator
                                                         attribute:NSLayoutAttributeTop
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:self
                                                         attribute:NSLayoutAttributeCenterY
                                                        multiplier:1
                                                          constant:2]];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_LevelIndicator
                                                         attribute:NSLayoutAttributeCenterX
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:self
                                                         attribute:NSLayoutAttributeCenterX
                                                        multiplier:1
                                                          constant:0]];
        
        m_Annotation = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 50, 20)];
        m_Annotation.translatesAutoresizingMaskIntoConstraints = false;
        m_Annotation.bordered = false;
        m_Annotation.editable = false;
        m_Annotation.drawsBackground = false;
        m_Annotation.font = [NSFont labelFontOfSize:11];
        m_Annotation.textColor = NSColor.secondaryLabelColor;
        m_Annotation.stringValue = @"How are we doing?"; // Localize!!!
        [self addSubview:m_Annotation];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_Annotation
                                                         attribute:NSLayoutAttributeBottom
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:self
                                                         attribute:NSLayoutAttributeCenterY
                                                        multiplier:1
                                                          constant:3]];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_Annotation
                                                         attribute:NSLayoutAttributeCenterX
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:self
                                                         attribute:NSLayoutAttributeCenterX
                                                        multiplier:1
                                                          constant:0]];
        
        m_DiscardButton = [[NSButton alloc] initWithFrame:NSMakeRect(277, 2, 14, 14)];
        m_DiscardButton.translatesAutoresizingMaskIntoConstraints = false;
        m_DiscardButton.image = [NSImage imageNamed:NSImageNameStopProgressFreestandingTemplate];
        m_DiscardButton.imagePosition = NSImageOnly;
        m_DiscardButton.buttonType = NSMomentaryChangeButton;
        m_DiscardButton.bordered = false;
        m_DiscardButton.target = self;
        m_DiscardButton.action = @selector(discardClicked:);
        ((NSButtonCell*)m_DiscardButton.cell).imageScaling = NSImageScaleProportionallyUpOrDown;
        [self addSubview:m_DiscardButton];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_DiscardButton
                                                         attribute:NSLayoutAttributeCenterY
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:self
                                                         attribute:NSLayoutAttributeCenterY
                                                        multiplier:1
                                                          constant:3]];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_DiscardButton
                                                         attribute:NSLayoutAttributeRight
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:self
                                                         attribute:NSLayoutAttributeRight
                                                        multiplier:1
                                                          constant:-12]];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_DiscardButton
                                                         attribute:NSLayoutAttributeWidth
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:nil
                                                         attribute:NSLayoutAttributeNotAnAttribute
                                                        multiplier:1
                                                          constant:14]];
        [self addConstraint:[NSLayoutConstraint constraintWithItem:m_DiscardButton
                                                         attribute:NSLayoutAttributeHeight
                                                         relatedBy:NSLayoutRelationEqual
                                                            toItem:nil
                                                         attribute:NSLayoutAttributeNotAnAttribute
                                                        multiplier:1
                                                          constant:14]];
        
        [self layoutSubtreeIfNeeded];
    }
    return self;
}

- (void)viewDidMoveToSuperview
{
    if( self.superview )
        [m_DiscardButton bind:@"hidden" toObject:self withKeyPath:@"mouseHover" options:@{NSValueTransformerNameBindingOption:NSNegateBooleanTransformerName}];
    else
        [m_DiscardButton unbind:@"hidden"];
}

- (void)ratingClicked:(id)sender
{
    m_Rating = (int)m_LevelIndicator.integerValue;
    [self commit];
}

- (void)discardClicked:(id)sender
{
    m_Rating = 0;
    [self commit];
}

- (void) commit
{
    AskingForRatingOverlayView *v = self;
    [self removeFromSuperview];
 
    const auto result = m_Rating;
    dispatch_to_main_queue([=]{
        FeedbackManager::Instance().CommitRatingOverlayResult(result);
    });
    
   v = nil; // at this moment ARC should kill us
}

- (void)updateTrackingAreas
{
    while( self.trackingAreas.count )
        [self removeTrackingArea:self.trackingAreas[0]];
    
    NSTrackingArea *trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                                options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInActiveApp
                                                                  owner:self
                                                               userInfo:nil];
    [self addTrackingArea:trackingArea];
}

- (void)mouseEntered:(NSEvent *)theEvent
{
    self.mouseHover = true;
}

- (void)mouseExited:(NSEvent *)theEvent
{
    self.mouseHover = false;
}

@end