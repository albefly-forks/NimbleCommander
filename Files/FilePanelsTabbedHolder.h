//
//  FilePanelsTabbedHolder.h
//  Files
//
//  Created by Michael G. Kazakov on 28/10/14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

@class MMTabBarView;
@class PanelView;
@class PanelController;

@interface FilePanelsTabbedHolder : NSStackView

@property (nonatomic, readonly) MMTabBarView *tabBar;
@property (nonatomic, readonly) NSTabView    *tabView;
@property (nonatomic, readonly) PanelView    *current; // can return nil in case if there's no panels inserted or in some other weird cases
@property (nonatomic, readonly) unsigned     tabsCount;

- (void) addPanel:(PanelView*)_panel;
- (NSTabViewItem*) tabViewItemForController:(PanelController*)_controller;

@end
