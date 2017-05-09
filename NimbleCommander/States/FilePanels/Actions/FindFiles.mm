#include <VFS/VFSListingInput.h>
#include <NimbleCommander/States/FilePanels/FindFilesSheetController.h>
#include "../PanelController.h"
#include "FindFiles.h"
#include "../PanelView.h"

namespace nc::panel::actions {

bool FindFiles::Predicate( PanelController *_target ) const
{
    return _target.isUniform || _target.view.item;
}

static shared_ptr<VFSListing> FetchSearchResultsAsListing(const vector<VFSPath> &_filepaths,
                                                          int _fetch_flags,
                                                          const VFSCancelChecker &_cancel_checker)
{
    vector<VFSListingPtr> listings;
    
    for( auto &p: _filepaths ) {
        VFSListingPtr listing;
        int ret = p.Host()->FetchSingleItemListing(p.Path().c_str(),
                                                   listing,
                                                   _fetch_flags,
                                                   _cancel_checker);
        if( ret == 0 )
            listings.emplace_back( listing );

        if( _cancel_checker && _cancel_checker() )
            return {};
    }
    
    return VFSListing::Build( VFSListing::Compose(listings) );
}

void FindFiles::Perform( PanelController *_target, id _sender ) const
{
    FindFilesSheetController *sheet = [FindFilesSheetController new];
    sheet.host = _target.isUniform ?
        _target.vfs :
        _target.view.item.Host();
    sheet.path = _target.isUniform ?
        _target.currentDirectoryPath :
        _target.view.item.Directory();
    sheet.onPanelize = [=](const vector<VFSPath> &_paths) {
        auto task = [=]( const function<bool()> &_cancelled ) {
            auto l = FetchSearchResultsAsListing(_paths,
                                                 _target.vfsFetchingFlags,
                                                 _cancelled
                                                 );
            if( l )
                dispatch_to_main_queue([=]{
                    [_target loadNonUniformListing:l];
                });
        
        };
        [_target commitCancelableLoadingTask:move(task)];
    };
    
    [sheet beginSheetForWindow:_target.window
             completionHandler:^(NSModalResponse returnCode) {
                 if(auto item = sheet.selectedItem)
                     [_target GoToDir:item->dir_path
                                  vfs:item->host
                         select_entry:item->filename
                                async:true];
    }];
}

};
