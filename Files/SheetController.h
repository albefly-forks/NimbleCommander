//
//  SheetController.h
//  Files
//
//  Created by Michael G. Kazakov on 05/08/14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface SheetController : NSWindowController

- (void) beginSheetForWindow:(NSWindow*)_wnd
           completionHandler:(void (^)(NSModalResponse returnCode))_handler;

- (void) endSheet:(NSModalResponse)returnCode;


@end
