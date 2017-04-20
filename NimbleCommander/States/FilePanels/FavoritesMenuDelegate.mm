#include <NimbleCommander/Core/AnyHolder.h>
#include "PanelDataPersistency.h"
#include "PanelController+Menu.h"
#include "Favorites.h"
#include "FavoritesMenuDelegate.h"

static NSMenuItem *BuildMenuItem( const FavoriteLocationsStorage::Favorite &_favorite )
{
    static const auto attributes = @{NSFontAttributeName:[NSFont menuFontOfSize:0]};
    NSMenuItem *it = [[NSMenuItem alloc] init];
    if( !_favorite.title.empty() ) {
        if( auto title = [NSString stringWithUTF8StdString:_favorite.title] )
            it.title = title;
    }
    else if( auto title = [NSString stringWithUTF8StdString:_favorite.location->verbose_path] )
        it.title = StringByTruncatingToWidth(title, 600, kTruncateAtMiddle, attributes);
    if( auto tt = [NSString stringWithUTF8StdString:_favorite.location->verbose_path] )
        it.toolTip = tt;
    
    it.target = nil;
    it.action = @selector(OnGoToFavoriteLocation:);
    it.representedObject = [[AnyHolder alloc] initWithAny:any(_favorite.location->hosts_stack)];
    return it;
}

static NSMenuItem *BuildMenuItem( const FavoriteLocationsStorage::Location &_location )
{
    NSMenuItem *it = [[NSMenuItem alloc] init];
    if( auto title = [NSString stringWithUTF8StdString:_location.verbose_path] )
        it.title = title;
    it.target = nil;
    it.action = @selector(OnGoToFavoriteLocation:);
    it.representedObject = [[AnyHolder alloc] initWithAny:any(_location.hosts_stack)];
    return it;
}

@implementation FavoriteLocationsMenuDelegate
{
    vector<NSMenuItem*> m_MenuItems;
    bool m_MenuIsDirty;
    FavoriteLocationsStorage *m_Storage;
    NSMenuItem* m_ManageItem;
    FavoriteLocationsStorage::ObservationTicket m_Ticket;
}

- (instancetype) initWithStorage:(FavoriteLocationsStorage&)_storage
               andManageMenuItem:(NSMenuItem *)_item;
{
   if( self = [super init] ) {
       assert(_item);
        m_Storage = &_storage;
        m_ManageItem = _item;
        [self refreshItems];
        m_Ticket = m_Storage->ObserveFavoritesChanges(
            objc_callback(self, @selector(favoritesChanged)) );
    }
    return self;
}

- (void)favoritesChanged
{
    [self refreshItems];
}

- (void) refreshItems
{
    m_MenuItems.clear();
    for( auto &f: m_Storage->Favorites() )
        m_MenuItems.emplace_back( BuildMenuItem(f) );
    m_MenuItems.emplace_back( NSMenuItem.separatorItem );
    m_MenuItems.emplace_back( m_ManageItem );
    m_MenuIsDirty = true;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if( m_MenuIsDirty ) {
        [menu removeAllItems];
        for(auto i: m_MenuItems)
            [menu addItem:i];
        m_MenuIsDirty = false;
    }
}

@end

@implementation FrequentlyVisitedLocationsMenuDelegate
{
    FavoriteLocationsStorage *m_Storage;
    NSMenuItem* m_ClearItem;
}

- (instancetype) initWithStorage:(FavoriteLocationsStorage&)_storage
               andClearMenuItem:(NSMenuItem *)_item
{
  if( self = [super init] ) {
       assert(_item);
        m_Storage = &_storage;
        m_ClearItem = _item;
        m_ClearItem.target = self;
        m_ClearItem.action = @selector(OnClearMenu:);
    }
    return self;
}

- (BOOL)menuHasKeyEquivalent:(NSMenu*)menu
                    forEvent:(NSEvent*)event
                      target:(__nullable id* __nullable)target
                      action:(__nullable SEL* __nullable)action
{
    return false; // this menu has no hotkeys, so there's no reason to (re)build it upon a keydown.
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    // caching means not much here, so we rebuild this menu on every access
    const auto locations = m_Storage->FrecentlyUsed(10);

    [menu removeAllItems];
    for( auto &l: locations )
        [menu addItem:BuildMenuItem(*l)];
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItem:m_ClearItem];
}

- (IBAction)OnClearMenu:(id)sender
{
    m_Storage->ClearVisitedLocations();
}

@end