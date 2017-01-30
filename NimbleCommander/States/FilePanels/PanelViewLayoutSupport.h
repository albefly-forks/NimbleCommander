#pragma once

#include <Habanero/Observable.h>
#include "Brief/Layout.h"
#include "List/Layout.h"

struct PanelViewDisabledLayout
{
    /* dummy layout, used to indicate that this layout is not active */
};

struct PanelViewLayout
{
    enum class Type : signed char
    {
        Disabled    = -1,
        Brief       = 0,
        List        = 1
        /*Thumbs = 2 */
    };
    
    string name; // for the future
    any layout; // perhaps switch to variant?
    // may be PanelListViewColumnsLayout, PanelBriefViewColumnsLayout or
    // PanelViewDisabledLayout at the moment.
    bool is_disabled() const;
    Type type() const;
    const PanelBriefViewColumnsLayout *brief() const;
    const PanelListViewColumnsLayout *list() const;
    
    bool operator==(const PanelViewLayout&) const;
    bool operator!=(const PanelViewLayout&) const;
};

// supposed to be thread-safe
class PanelViewLayoutsStorage : public ObservableBase
{
public:
    PanelViewLayoutsStorage( const char*_config_path );
    
    /**
     * Will return total layouts count, including disabled onces (PanelViewDisabledLayout).
     */
    int LayoutsCount() const;
    
    /**
     * Will return nullptr on invalid index.
     */
    shared_ptr<const PanelViewLayout> GetLayout( int _index ) const;
    
    /**
     * Get all layouts this storage has.
     */
    vector<shared_ptr<const PanelViewLayout>> GetAllLayouts() const;

    /**
     * Will ignore requests on invalid index.
     */
    void ReplaceLayout(PanelViewLayout _layout, int _at_index);
    
    /**
     * Should be used when panel is forced to use a disabled layout.
     */
    const shared_ptr<const PanelViewLayout>& LastResortLayout() const;
    
    using ObservationTicket = ObservableBase::ObservationTicket;
    ObservationTicket ObserveChanges( function<void()> _callback );
    
private:
    void LoadLayoutsFromConfig();
    void WriteLayoutsToConfig() const;
    void CommitChanges();
        
    mutable spinlock                            m_LayoutsLock;
    vector<shared_ptr<const PanelViewLayout>>   m_Layouts;
    const char*                                 m_ConfigPath;    
};

@interface PanelViewLayoutsMenuDelegate : NSObject<NSMenuDelegate>

- (id) initWithStorage:(const PanelViewLayoutsStorage&)_storage;

@end