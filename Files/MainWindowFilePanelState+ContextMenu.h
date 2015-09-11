//
//  MainWindowFilePanelState+ContextMenu.h
//  Files
//
//  Created by Michael G. Kazakov on 07.11.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import "MainWindowFilePanelState.h"
#import "VFS.h"

@interface MainWindowFilePanelState (ContextMenu)

/**
 * _items: temporary items info for quick decisions, will not be stored
 *
 */
- (NSMenu*) RequestContextMenuOn:(vector<VFSFlexibleListingItem>) _items
                            path:(const char*) _path
                             vfs:(shared_ptr<VFSHost>) _host
                          caller:(PanelController*) _caller;

@end
