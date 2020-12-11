// Copyright (C) 2016-2020 Michael Kazakov. Subject to GNU General Public License version 3.
#include "../Bootstrap/ActivationManager.h"
#include "../Core/FeedbackManager.h"
#include "FeedbackWindow.h"
#include <chrono>
#include <Habanero/dispatch_cpp.h>

using namespace std::literals;

@interface FeedbackWindow ()
@property (nonatomic) IBOutlet NSTabView *tabView;

@end

@implementation FeedbackWindow
{
    FeedbackWindow *m_Self;
    nc::bootstrap::ActivationManager *m_ActivationManager;
}

@synthesize rating;

- (instancetype)initWithActivationManager:(nc::bootstrap::ActivationManager&)_am
{
    self = [super initWithWindowNibName:NSStringFromClass(self.class)];
    if( self ) {
        m_ActivationManager = &_am;
        self.rating = 1;
    }
    return self;
}

- (void)windowDidLoad
{
    [super windowDidLoad];
    m_Self = self;
    
    if( self.rating == 5 || self.rating == 4) {
        // positive branch
        if( m_ActivationManager->ForAppStore() )
            [self.tabView selectTabViewItemAtIndex:0];
        else
            [self.tabView selectTabViewItemAtIndex:1];
    }
    else if( self.rating == 3 || self.rating == 2 ) {
        // neutral branch
        [self.tabView selectTabViewItemAtIndex:2];
    }
    else {
        // negative branch
        [self.tabView selectTabViewItemAtIndex:3];
    }
}

- (void)windowWillClose:(NSNotification *)[[maybe_unused]]_notification
{
    dispatch_to_main_queue_after(10ms, [=]{
        m_Self = nil;
    });
}

- (IBAction)onEmailFeedback:(id)[[maybe_unused]]_sender
{
    FeedbackManager::Instance().EmailFeedback();
}

- (IBAction)onHelp:(id)[[maybe_unused]]_sender
{
    FeedbackManager::Instance().EmailSupport();
}

- (IBAction)onRate:(id)[[maybe_unused]]_sender
{
    FeedbackManager::Instance().RateOnAppStore();
}

@end
