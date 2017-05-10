#include "ConnectionsMenuDelegate.h"
#include <NimbleCommander/States/FilePanels/PanelController.h>
#include <NimbleCommander/States/FilePanels/PanelController+Menu.h>
#include <NimbleCommander/Core/AnyHolder.h>
#include "NetworkConnectionsManager.h"

@interface ConnectionsMenuDelegate()

@property (strong) IBOutlet NSMenuItem *recentConnectionsMenuItem;

@end

@implementation ConnectionsMenuDelegate
{
    vector<NetworkConnectionsManager::Connection> m_Connections;
    function<NetworkConnectionsManager&()> m_Manager;
    int m_InitialElementsCount;
}

- (instancetype) initWithManager:(function<NetworkConnectionsManager&()>)_callback
{
    self = [super init];
    if( self ) {
        m_Manager = _callback;
        m_InitialElementsCount = -1;
    }
    return self;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if( (menu.propertiesToUpdate & NSMenuPropertyItemTitle) == 0 )
        return; // we should update this menu only when it's shown, not by a key press
  
    if( m_InitialElementsCount < 0 )
        m_InitialElementsCount = (int)menu.numberOfItems;
  
    // there's no "dirty" state for MRU, so need to fetch data on every access current state
    // and compare it with previously used. this is ugly, but it's much better than rebuilding whole
    // menu every time. Better approach would be making NetworkConnectionsManager observable.
    auto &ncm = m_Manager();
    auto connections = ncm.AllConnectionsByMRU();
    if( m_Connections != connections ) {
        m_Connections = move(connections);
    
        while( menu.numberOfItems > m_InitialElementsCount )
            [menu removeItemAtIndex:menu.numberOfItems-1];
        
        self.recentConnectionsMenuItem.hidden = m_Connections.empty();
        
        for( auto &c: m_Connections ) {
            NSMenuItem *regular_item = [[NSMenuItem alloc] init];
            regular_item.indentationLevel = 1;
            regular_item.title = [NSString stringWithUTF8StdString:ncm.TitleForConnection(c)];
            regular_item.representedObject = [[AnyHolder alloc] initWithAny:any{c}];
            regular_item.action = @selector(OnGoToSavedConnectionItem:);
            [menu addItem:regular_item];
        }
    }
}

@end
