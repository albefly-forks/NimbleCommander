#include "PanelDataExternalEntryKey.h"
#include <VFS/VFSListing.h>
#include "PanelDataItemVolatileData.h"

namespace panel {

ExternalEntryKey::ExternalEntryKey():
    name{""},
    display_name{nil},
    extension{""},
    size{0},
    mtime{0},
    btime{0},
    add_time{-1},
    is_dir{false}
{
}

ExternalEntryKey::ExternalEntryKey(const VFSListingItem& _item,
                                   const PanelDataItemVolatileData &_item_vd):
    ExternalEntryKey()
{
    name = _item.Name();
    display_name = _item.NSDisplayName();
    extension = _item.HasExtension() ? _item.Extension() : "";
    size = _item_vd.size;
    mtime = _item.MTime();
    btime = _item.BTime();
    add_time = _item.HasAddTime() ? _item.AddTime() : -1;
    is_dir = _item.IsDir();
}

bool ExternalEntryKey::is_valid() const noexcept
{
    return !name.empty() && display_name != nil;
}


}