//
//  SheetController.m
//  Files
//
//  Created by Michael G. Kazakov on 05/08/14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#include "SheetController.h"
#include "Common.h"

@implementation SheetController
{
    __strong SheetController *m_Self;
}

- (id) init
{
    self = [super initWithWindowNibName:NSStringFromClass(self.class)];
    if(self) {
    }
    return self;
}

- (void) beginSheetForWindow:(NSWindow*)_wnd
           completionHandler:(void (^)(NSModalResponse returnCode))_handler
{
    if(!dispatch_is_main_queue()) {
        dispatch_to_main_queue([=]{
            [self beginSheetForWindow:_wnd completionHandler:_handler];
        });
        return;
    }
    
    assert(_handler != nil);
    m_Self = self;
    
    [_wnd beginSheet:self.window completionHandler:_handler];
}

- (void) endSheet:(NSModalResponse)returnCode
{
    bool release_self = m_Self != nil;
    
    [self.window.sheetParent endSheet:self.window returnCode:returnCode];
    
    if(release_self)
        dispatch_to_main_queue_after(1ms, [=]{
            m_Self = nil;
        });
}

@end
