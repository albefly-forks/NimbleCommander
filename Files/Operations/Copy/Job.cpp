//
//  FileCopyOperationNew.cpp
//  Files
//
//  Created by Michael G. Kazakov on 25/09/15.
//  Copyright © 2015 Michael G. Kazakov. All rights reserved.
//

#include <sys/xattr.h>
#include <Habanero/algo.h>
#include <Habanero/Hash.h>
#include <Utility/PathManip.h>
#include <RoutedIO/RoutedIO.h>
#include "Job.h"
#include "DialogResults.h"

static bool ShouldPreallocateSpace(int64_t _bytes_to_write, const NativeFileSystemInfo &_fs_info)
{
    const auto min_prealloc_size = 4096;
    if( _bytes_to_write <= min_prealloc_size )
        return false;

    // need to check destination fs and permit preallocation only on certain filesystems
    return _fs_info.fs_type_name == "hfs"; // Apple's copyfile() also uses preallocation on Xsan volumes
}

// PreallocateSpace assumes following ftruncate, meaningless otherwise
static void PreallocateSpace(int64_t _preallocate_delta, int _file_des)
{
    fstore_t preallocstore = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, _preallocate_delta};
    if( fcntl(_file_des, F_PREALLOCATE, &preallocstore) == -1 ) {
        preallocstore.fst_flags = F_ALLOCATEALL;
        fcntl(_file_des, F_PREALLOCATE, &preallocstore);
    }
}

static void AdjustFileTimesForNativePath(const char* _target_path, struct stat &_with_times)
{
    struct attrlist attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.bitmapcount = ATTR_BIT_MAP_COUNT;
    
    attrs.commonattr = ATTR_CMN_MODTIME;
    setattrlist(_target_path, &attrs, &_with_times.st_mtimespec, sizeof(struct timespec), 0);
    
    attrs.commonattr = ATTR_CMN_CRTIME;
    setattrlist(_target_path, &attrs, &_with_times.st_birthtimespec, sizeof(struct timespec), 0);
    
    //  do we really need atime to be changed?
    //    attrs.commonattr = ATTR_CMN_ACCTIME;
    //    fsetattrlist(_target_fd, &attrs, &_with_times.st_atimespec, sizeof(struct timespec), 0);
    
    attrs.commonattr = ATTR_CMN_CHGTIME;
    setattrlist(_target_path, &attrs, &_with_times.st_ctimespec, sizeof(struct timespec), 0);
}

static void AdjustFileTimesForNativePath(const char* _target_path, const VFSStat &_with_times)
{
    auto st = _with_times.SysStat();
    AdjustFileTimesForNativePath(_target_path, st);
}

static void AdjustFileTimesForNativeFD(int _target_fd, struct stat &_with_times)
{
    struct attrlist attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.bitmapcount = ATTR_BIT_MAP_COUNT;
    
    attrs.commonattr = ATTR_CMN_MODTIME;
    fsetattrlist(_target_fd, &attrs, &_with_times.st_mtimespec, sizeof(struct timespec), 0);
    
    attrs.commonattr = ATTR_CMN_CRTIME;
    fsetattrlist(_target_fd, &attrs, &_with_times.st_birthtimespec, sizeof(struct timespec), 0);

//  do we really need atime to be changed?
//    attrs.commonattr = ATTR_CMN_ACCTIME;
//    fsetattrlist(_target_fd, &attrs, &_with_times.st_atimespec, sizeof(struct timespec), 0);
    
    attrs.commonattr = ATTR_CMN_CHGTIME;
    fsetattrlist(_target_fd, &attrs, &_with_times.st_ctimespec, sizeof(struct timespec), 0);
}

static void AdjustFileTimesForNativeFD(int _target_fd, const VFSStat &_with_times)
{
    auto st = _with_times.SysStat();
    AdjustFileTimesForNativeFD(_target_fd, st);
}

static string FilenameFromPath(string _str)
{
    if( _str.empty() )
        return _str;
    auto sl = _str.find_last_of('/');
    if( sl == _str.npos )
        return _str;
    _str.erase(begin(_str), begin(_str)+sl+1);
    return _str;
}

static optional<FileCopyOperationOptions::ExistBehavior> DialogResultToExistBehavior( int _dr )
{
    switch( _dr ) {
        case FileCopyOperationDR::Overwrite:    return FileCopyOperationOptions::ExistBehavior::OverwriteAll;
        case FileCopyOperationDR::OverwriteOld: return FileCopyOperationOptions::ExistBehavior::OverwriteOld;
        case FileCopyOperationDR::Append:       return FileCopyOperationOptions::ExistBehavior::AppendAll;
        case FileCopyOperationDR::Skip:         return FileCopyOperationOptions::ExistBehavior::SkipAll;
        default:                                return nullopt;
    }
}

FileCopyOperationJob::ChecksumExpectation::ChecksumExpectation( int _source_ind, string _destination, const vector<uint8_t> &_md5 ):
    original_item( _source_ind ),
    destination_path( move(_destination) )
{
    if(_md5.size() != 16)
        throw invalid_argument("FileCopyOperationJobNew::ChecksumExpectation::ChecksumExpectation: _md5 should be 16 bytes long!");
    copy(begin(_md5), end(_md5), begin(md5.buf));
}

void FileCopyOperationJob::Init(vector<VFSListingItem> _source_items,
                                   const string &_dest_path,
                                   const VFSHostPtr &_dest_host,
                                   FileCopyOperationOptions _opts)
{
    m_VFSListingItems = move(_source_items);
    m_InitialDestinationPath = _dest_path;
    if( m_InitialDestinationPath.empty() || m_InitialDestinationPath.front() != '/' )
        throw invalid_argument("FileCopyOperationJobNew::Init: m_InitialDestinationPath should be an absolute path");
    m_DestinationHost = _dest_host;
    m_Options = _opts;
    m_IsSingleInitialItemProcessing = m_VFSListingItems.size() == 1;
        
    if( m_VFSListingItems.empty() )
        cerr << "FileCopyOperationJobNew::Init(..) was called with an empty entries list!" << endl;
}

bool FileCopyOperationJob::IsSingleInitialItemProcessing() const noexcept
{
    return m_IsSingleInitialItemProcessing;
}

bool FileCopyOperationJob::IsSingleScannedItemProcessing() const noexcept
{
    return m_IsSingleScannedItemProcessing;
}

FileCopyOperationJob::JobStage FileCopyOperationJob::Stage() const noexcept
{
    return m_Stage;
}

void FileCopyOperationJob::ToggleSkipAll()
{
    m_SkipAll = true;
}

void FileCopyOperationJob::ToggleExistBehaviorSkipAll()
{
    m_Options.exist_behavior = FileCopyOperationOptions::ExistBehavior::SkipAll;
}

void FileCopyOperationJob::ToggleExistBehaviorOverwriteAll()
{
    m_Options.exist_behavior = FileCopyOperationOptions::ExistBehavior::OverwriteAll;
}

void FileCopyOperationJob::ToggleExistBehaviorOverwriteOld()
{
    m_Options.exist_behavior = FileCopyOperationOptions::ExistBehavior::OverwriteOld;
}

void FileCopyOperationJob::ToggleExistBehaviorAppendAll()
{
    m_Options.exist_behavior = FileCopyOperationOptions::ExistBehavior::AppendAll;
}

void FileCopyOperationJob::Do()
{
    SetState(JobStage::Preparing);

    bool need_to_build = false;
    auto comp_type = AnalyzeInitialDestination(m_DestinationPath, need_to_build);
    if( need_to_build )
        BuildDestinationDirectory();
    m_PathCompositionType = comp_type;
 
    if( m_DestinationHost->IsNativeFS() )
        if( !(m_DestinationNativeFSInfo = NativeFSManager::Instance().VolumeFromPath(m_DestinationPath)) ) {
            m_DestinationNativeFSInfo = NativeFSManager::Instance().VolumeFromPathFast(m_DestinationPath); // this may be wrong in case of symlinks
            if( !m_DestinationNativeFSInfo ) {
                SetStopped(); // we're totally fucked. can't go on
                return;
            }
        }
    
    auto scan_result = ScanSourceItems();
    if( get<0>(scan_result) != StepResult::Ok ) {
        SetStopped();
        return;
    }
    m_SourceItems = move( get<1>(scan_result) );
    
    m_IsSingleScannedItemProcessing = m_SourceItems.ItemsAmount() == 1;
    
    m_VFSListingItems.clear(); // don't need them anymore
    
    ProcessItems();
    
    if( CheckPauseOrStop() ) { SetStopped(); return; }
    SetCompleted();
}

void FileCopyOperationJob::ProcessItems()
{
    SetState(JobStage::Process);
    m_Stats.StartTimeTracking();
    m_Stats.SetMaxValue( m_SourceItems.TotalRegBytes() );
    
    const bool dest_host_is_native = m_DestinationHost->IsNativeFS();
    auto is_same_native_volume = [this]( int _index ) {
        return NativeFSManager::Instance().VolumeFromDevID( m_SourceItems.ItemDev(_index) ) == m_DestinationNativeFSInfo;
    };
    
    for( int index = 0, index_end = m_SourceItems.ItemsAmount(); index != index_end; ++index ) {
        auto source_mode = m_SourceItems.ItemMode(index);
        auto&source_host = m_SourceItems.ItemHost(index);
        auto source_size = m_SourceItems.ItemSize(index);
        auto destination_path = ComposeDestinationNameForItem(index);
        auto source_path = m_SourceItems.ComposeFullPath(index);
        
        StepResult step_result = StepResult::Stop;
        
        m_Stats.SetCurrentItem( m_SourceItems.ItemName(index) );
        
        if( S_ISREG(source_mode) ) {
            /////////////////////////////////////////////////////////////////////////////////////////////////
            // Regular files
            /////////////////////////////////////////////////////////////////////////////////////////////////
            optional<Hash> hash; // this optional will be filled with the first call of hash_feedback
            auto hash_feedback = [&](const void *_data, unsigned _sz) {
                if( !hash )
                    hash.emplace(Hash::MD5);
                hash->Feed( _data, _sz );
            };

            function<void(const void *_data, unsigned _sz)> data_feedback = nullptr;
            if( m_Options.verification == ChecksumVerification::Always )
                data_feedback = hash_feedback;
            else if( !m_Options.docopy && m_Options.verification >= ChecksumVerification::WhenMoves )
                data_feedback = hash_feedback;
            
            if( source_host.IsNativeFS() && dest_host_is_native ) { // native -> native ///////////////////////
                // native fs processing
                if( m_Options.docopy ) { // copy
                    step_result = CopyNativeFileToNativeFile(source_path, destination_path, data_feedback);
                }
                else {
                    if( is_same_native_volume(index) ) { // rename
                        step_result = RenameNativeFile(source_path, destination_path);
                        if( step_result == StepResult::Ok )
                            m_Stats.AddValue( source_size );
                    }
                    else { // move
                        step_result = CopyNativeFileToNativeFile(source_path, destination_path, data_feedback);
                        if( step_result == StepResult::Ok )
                            m_SourceItemsToDelete.emplace_back(index); // mark source file for deletion
                    }
                }
            }
            else if( dest_host_is_native  ) { // vfs -> native ///////////////////////////////////////////////
                if( m_Options.docopy ) { // copy
                    step_result = CopyVFSFileToNativeFile(source_host, source_path, destination_path, data_feedback);
                }
                else { // move
                    step_result = CopyVFSFileToNativeFile(source_host, source_path, destination_path, data_feedback);
                    if( step_result == StepResult::Ok )
                        m_SourceItemsToDelete.emplace_back(index); // mark source file for deletion
                }
            }
            else { // vfs -> vfs /////////////////////////////////////////////////////////////////////////////
                if( m_Options.docopy ) { // copy
                    step_result = CopyVFSFileToVFSFile(source_host, source_path, destination_path, data_feedback);
                }
                else { // move
                    if( &source_host == m_DestinationHost.get() ) { // rename
                        // moving on the same host - lets do rename
                        step_result = RenameVFSFile(source_host, source_path, destination_path);
                        if( step_result == StepResult::Ok )
                            m_Stats.AddValue( source_size );
                    }
                    else { // move
                        step_result = CopyVFSFileToVFSFile(source_host, source_path, destination_path, data_feedback);
                        if( step_result == StepResult::Ok )
                            m_SourceItemsToDelete.emplace_back(index); // mark source file for deletion
                    }
                }
            }
            
            // check step result?
            if( hash )
                m_Checksums.emplace_back( index, destination_path, hash->Final() );
        }
        else if( S_ISDIR(source_mode) ) {
            /////////////////////////////////////////////////////////////////////////////////////////////////
            // Directories
            /////////////////////////////////////////////////////////////////////////////////////////////////
            if( source_host.IsNativeFS() && dest_host_is_native ) { // native -> native
                if( m_Options.docopy ) { // copy
                    step_result = CopyNativeDirectoryToNativeDirectory(source_path, destination_path);
                }
                else { // move
                    if( is_same_native_volume(index) ) { // rename
                        step_result = RenameNativeFile(source_path, destination_path);
                    }
                    else { // move
                        step_result = CopyNativeDirectoryToNativeDirectory(source_path, destination_path);
                        if( step_result == StepResult::Ok )
                            m_SourceItemsToDelete.emplace_back(index); // mark source file for deletion
                    }
                }
            }
            else if( dest_host_is_native  ) { // vfs -> native
                step_result = CopyVFSDirectoryToNativeDirectory(source_host, source_path, destination_path);
                if( !m_Options.docopy && step_result == StepResult::Ok )
                    m_SourceItemsToDelete.emplace_back(index); // mark source file for deletion
            }
            else {
                if( m_Options.docopy ) { // copy
                    step_result = CopyVFSDirectoryToVFSDirectory(source_host, source_path, destination_path);
                }
                else { // move
                    if( &source_host == m_DestinationHost.get() ) { // moving on the same host - lets do rename
                        step_result = RenameVFSFile(source_host, source_path, destination_path);
                    }
                    else {
                        step_result = CopyVFSDirectoryToVFSDirectory(source_host, source_path, destination_path);
                        if( !m_Options.docopy && step_result == StepResult::Ok )
                            m_SourceItemsToDelete.emplace_back(index); // mark source file for deletion
                    }
                }
                
            }
        }
        else if( S_ISLNK(source_mode) ) {
            if( source_host.IsNativeFS() && dest_host_is_native ) { // native -> native
                step_result = CopyNativeSymlinkToNative(source_path, destination_path);
            }
            else if( dest_host_is_native  ) { // vfs -> native
                step_result = CopyVFSSymlinkToNative(source_host, source_path, destination_path);
            }
            else {
                /* NOT SUPPORTED YET */
            }
            
            
            
        }

        // check current item result
        if( step_result == StepResult::Stop || CheckPauseOrStop() )
            return;
        
        if( step_result == StepResult::SkipAll )
            ToggleSkipAll();
    }

    m_Stats.SetCurrentItem("");
    
    bool all_matched = true;
    if( !m_Checksums.empty() ) {
        SetState(JobStage::Verify);
        for( auto &item: m_Checksums ) {
            bool matched = false;
            m_Stats.SetCurrentItem( FilenameFromPath(item.destination_path) );
            auto step_result = VerifyCopiedFile(item, matched);
            if( step_result != StepResult::Ok || matched != true ) {
                m_OnFileVerificationFailed( item.destination_path );
                all_matched = false;
            }
        }
    }
    
    if( CheckPauseOrStop() ) return;
    
    // be sure to all it only if ALL previous steps wre OK.
    if( all_matched ) {
        SetState(JobStage::Cleaning);
        CleanSourceItems();
    }
}

string FileCopyOperationJob::ComposeDestinationNameForItem( int _src_item_index ) const
{
//    PathPreffix, // path = dest_path + source_rel_path
//    FixedPath    // path = dest_path + [source_rel_path without heading]
    if( m_PathCompositionType == PathCompositionType::PathPreffix ) {
        auto path = m_SourceItems.ComposeRelativePath(_src_item_index);
        path.insert(0, m_DestinationPath);
        return path;
    }
    else {
        auto result = m_DestinationPath;
        auto src = m_SourceItems.ComposeRelativePath(_src_item_index);
        if( m_IsSingleInitialItemProcessing ) {
            // for top level we need to just leave path without changes - skip top level's entry name.
            // for nested entries we need to cut first part of a path.
            //            if(strchr(_path, '/') != 0)
            //                strcat(destinationpath, strchr(_path, '/'));
            //        }
            auto sl = src.find('/');
            if( sl != src.npos )
                result += src.c_str() + sl;
        }
        return result;
    }
}

// side-effects: none.
static bool IsSingleDirectoryCaseRenaming( const FileCopyOperationOptions &_options, const vector<VFSListingItem> &_items, const VFSHostPtr& _dest_host, const VFSStat &_dest_stat )
{
    return  S_ISDIR(_dest_stat.mode)            &&
            _options.docopy == false            &&
            _items.size() == 1                  &&
            _items.front().Host()->IsNativeFS() &&
            _items.front().Host() == _dest_host &&
            _items.front().IsDir()              &&
            _items.front().Inode() == _dest_stat.inode;
}

FileCopyOperationJob::PathCompositionType FileCopyOperationJob::AnalyzeInitialDestination(string &_result_destination, bool &_need_to_build) const
{
    VFSStat st;
    if( m_DestinationHost->Stat(m_InitialDestinationPath.c_str(), st, 0, nullptr ) == 0) {
        // destination entry already exist
        if( S_ISDIR(st.mode) &&
            !IsSingleDirectoryCaseRenaming(m_Options, m_VFSListingItems, m_DestinationHost, st) // special exception for renaming a single directory on native case-insensitive fs
           ) {
            _result_destination = EnsureTrailingSlash( m_InitialDestinationPath );
            return PathCompositionType::PathPreffix;
        }
        else {
            _result_destination = m_InitialDestinationPath;
            return PathCompositionType::FixedPath; // if we have more than one item - it will cause "item already exist" on a second one
        }
    }
    else {
        // TODO: check single-item mode here?
        // destination entry is non-existent
        _need_to_build = true;
        if( m_InitialDestinationPath.back() == '/' || m_VFSListingItems.size() > 1 ) {
            // user want to copy/rename/move file(s) to some directory, like "/bin/Abra/Carabra/"
            // OR user want to copy/rename/move file(s) to some directory, like "/bin/Abra/Carabra" and have MANY items to copy/rename/move
            _result_destination = EnsureTrailingSlash( m_InitialDestinationPath );
            return PathCompositionType::PathPreffix;
        }
        else {
            // user want to copy/rename/move file/dir to some filename, like "/bin/abra"
            _result_destination = m_InitialDestinationPath;
            return PathCompositionType::FixedPath;
        }
    }
}


template <class T>
static void ReverseForEachDirectoryInString(const string& _path, T _t)
{
    size_t range_end = _path.npos;
    size_t last_slash;
    while( ( last_slash = _path.find_last_of('/', range_end) ) != _path.npos ) {
        if( !_t(_path.substr(0, last_slash+1)) )
            break;
        if( last_slash == 0)
            break;
        range_end = last_slash - 1;
    }
}

// build directories for every entrance of '/' in m_DestinationPath
// for /bin/abra/cadabra/ will check and build: /bin, /bin/abra, /bin/abra/cadabra
// for /bin/abra/cadabra  will check and build: /bin, /bin/abra
FileCopyOperationJob::StepResult FileCopyOperationJob::BuildDestinationDirectory() const
{
    // find directories to build
    vector<string> paths_to_build;
    ReverseForEachDirectoryInString( m_DestinationPath, [&](string _path) {
        if( !m_DestinationHost->Exists(_path.c_str()) ) {
            paths_to_build.emplace_back(move(_path));
            return true;
        }
        else
            return false;
    });
    
    // found directories are in a reverse order, so reverse this list
    reverse(begin(paths_to_build), end(paths_to_build));

    // build absent directories. no skipping here - all or nothing.
    constexpr mode_t new_dir_mode = S_IXUSR|S_IXGRP|S_IXOTH|S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR;
    for( auto &path: paths_to_build ) {
        int ret = 0;
        while( (ret = m_DestinationHost->CreateDirectory(path.c_str(), new_dir_mode, nullptr)) < 0 ) {
            switch( m_OnCantCreateDestinationRootDir( ret, path ) ) {
                case FileCopyOperationDR::Retry:    continue;
                default:                            return StepResult::Stop;
            }
        }
    }
    
    return StepResult::Ok;
}

static bool IsAnExternalExtenedAttributesStorage( VFSHost &_host, const string &_path, const string& _item_name, const VFSStat &_st )
{
    // currently we think that ExtEAs can be only on native VFS
    if( !_host.IsNativeFS() )
        return false;
    
    // any ExtEA should have ._Filename format
    auto cstring = _item_name.c_str();
    if( cstring[0] != '.' || cstring[1] != '_' || cstring[2] == 0 )
        return false;
    
    // check if current filesystem uses external eas
    auto fs_info = NativeFSManager::Instance().VolumeFromDevID( _st.dev );
    if( !fs_info || fs_info->interfaces.extended_attr == true )
        return false;
    
    // check if a 'main' file exists
    char path[MAXPATHLEN];
    strcpy(path, _path.c_str());
    
    // some magick to produce /path/subpath/filename from a /path/subpath/._filename
    char *last_dst = strrchr(path, '/');
    if( !last_dst )
        return false;
    strcpy( last_dst + 1, cstring + 2 );
    
    return _host.Exists( path );
}

tuple<FileCopyOperationJob::StepResult, FileCopyOperationJob::SourceItems> FileCopyOperationJob::ScanSourceItems() const
{
    
    SourceItems db;
    auto stat_flags = m_Options.preserve_symlinks ? VFSFlags::F_NoFollow : 0;

    for( auto&i: m_VFSListingItems ) {
        if( CheckPauseOrStop() )
            return StepResult::Stop;
        
        auto host_indx = db.InsertOrFindHost(i.Host());
        auto &host = db.Host(host_indx);
        auto base_dir_indx = db.InsertOrFindBaseDir(i.Directory());
        function<StepResult(int _parent_ind, const string &_full_relative_path, const string &_item_name)> // need function holder for recursion to work
        scan_item = [this, &db, stat_flags, host_indx, &host, base_dir_indx, &scan_item] (int _parent_ind,
                                                                                          const string &_full_relative_path,
                                                                                          const string &_item_name
                                                                                          ) -> StepResult {
            // compose a full path for current entry
            string path = db.BaseDir(base_dir_indx) + _full_relative_path;
            
            // gather stat() information regarding current entry
            int ret;
            VFSStat st;
            while( (ret = host.Stat(path.c_str(), st, stat_flags, nullptr)) < 0 ) {
                if( m_SkipAll ) return StepResult::Skipped;
                switch( m_OnCantAccessSourceItem(ret, path) ) {
                    case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                    case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                    case FileCopyOperationDR::Stop:       return StepResult::Stop;
                }
            }
            
            if( S_ISREG(st.mode) ) {
                // check if file is an external EA
                if( IsAnExternalExtenedAttributesStorage(host, path, _item_name, st) )
                    return StepResult::Ok; // we're skipping "._xxx" files as they are processed by OS itself when we copy xattrs
                
                db.InsertItem(host_indx, base_dir_indx, _parent_ind, _item_name, st);
            }
            else if( S_ISLNK(st.mode) ) {
                db.InsertItem(host_indx, base_dir_indx, _parent_ind, _item_name, st);
            }
            else if( S_ISDIR(st.mode) ) {
                int my_indx = db.InsertItem(host_indx, base_dir_indx, _parent_ind, _item_name, st);
                
                bool should_go_inside =
                    m_Options.docopy ||
                    &host != &*m_DestinationHost || // comparing hosts by their addresses. which is NOT GREAT at all
                    (m_DestinationHost->IsNativeFS() && m_DestinationNativeFSInfo != NativeFSManager::Instance().VolumeFromDevID(st.dev) );
                if( should_go_inside ) {
                    vector<string> dir_ents;
                    while( (ret = host.IterateDirectoryListing(path.c_str(), [&](auto &_) { return dir_ents.emplace_back(_.name), true; })) < 0 ) {
                        if( m_SkipAll ) return StepResult::Skipped;
                        switch( m_OnCantAccessSourceItem(ret, path) ) {
                            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                            case FileCopyOperationDR::Stop:       return StepResult::Stop;
                        }
                        dir_ents.clear();
                    }
                    
                    for( auto &entry: dir_ents ) {
                        if( CheckPauseOrStop() )
                            return StepResult::Stop;
                        
                        // go into recursion
                        scan_item(my_indx,
                                  _full_relative_path + '/' + entry,
                                  entry);
                    }
                }
            }
            
            return StepResult::Ok;
        };
        
        auto result = scan_item(-1,
                                i.Filename(),
                                i.Filename()
                                );
        if( result != StepResult::Ok )
            return result;
    }
    
    return {StepResult::Ok, move(db)};
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////
// native file -> native file copying routine
//////////////////////////////////////////////////////////////////////////////////////////////////////////
FileCopyOperationJob::StepResult FileCopyOperationJob::CopyNativeFileToNativeFile(const string& _src_path,
                                                                                        const string& _dst_path,
                                                                                        function<void(const void *_data, unsigned _sz)> _source_data_feedback) const
{
    auto &io = RoutedIO::Default;
    
    // we initially open source file in non-blocking mode, so we can fail early and not to cause a hang. (hi, apple!)
    int source_fd = -1;
    while( (source_fd = io.open(_src_path.c_str(), O_RDONLY|O_NONBLOCK|O_SHLOCK)) == -1 &&
           (source_fd = io.open(_src_path.c_str(), O_RDONLY|O_NONBLOCK)) == -1 ) {
        // failed to open source file
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( VFSError::FromErrno(), _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }

    // be sure to close source file descriptor
    auto close_source_fd = at_scope_end([&]{
        if( source_fd >= 0 )
            close( source_fd );
    });

    // do not waste OS file cache with one-way data
    fcntl(source_fd, F_NOCACHE, 1);

    // get current file descriptor's open flags
    {
        int fcntl_ret = fcntl(source_fd, F_GETFL);
        if( fcntl_ret < 0 )
            throw runtime_error("fcntl(source_fd, F_GETFL) returned a negative value"); // <- if this happens then we're deeply in asshole

        // exclude non-blocking flag for current descriptor, so we will go straight blocking sync next
        fcntl_ret = fcntl(source_fd, F_SETFL, fcntl_ret & ~O_NONBLOCK);
        if( fcntl_ret < 0 )
            throw runtime_error("fcntl(source_fd, F_SETFL, fcntl_ret & ~O_NONBLOCK) returned a negative value"); // <- -""-
    }
    
    // get information about source file
    struct stat src_stat_buffer;
    while( fstat(source_fd, &src_stat_buffer) == -1 ) {
        // failed to stat source
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( VFSError::FromErrno(), _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }
  
    // find fs info for source file.
    auto src_fs_info_holder = NativeFSManager::Instance().VolumeFromDevID( src_stat_buffer.st_dev );
    if( !src_fs_info_holder )
        return StepResult::Stop; // something VERY BAD has happened, can't go on
    auto &src_fs_info = *src_fs_info_holder;
    
    // setting up copying scenario
    int     dst_open_flags          = 0;
    bool    do_erase_xattrs         = false,
            do_copy_xattrs          = true,
            do_unlink_on_stop       = false,
            do_set_times            = true,
            do_set_unix_flags       = true,
            need_dst_truncate       = false,
            dst_existed_before      = false;
    int64_t dst_size_on_stop        = 0,
            total_dst_size          = src_stat_buffer.st_size,
            preallocate_delta       = 0,
            initial_writing_offset  = 0;
    
    // stat destination
    struct stat dst_stat_buffer;
    if( io.stat(_dst_path.c_str(), &dst_stat_buffer) != -1 ) {
        // file already exist. what should we do now?
        dst_existed_before = true;
        const auto setup_overwrite = [&]{
            dst_open_flags = O_WRONLY;
            do_unlink_on_stop = true;
            dst_size_on_stop = 0;
            do_erase_xattrs = true;
            preallocate_delta = src_stat_buffer.st_size - dst_stat_buffer.st_size; // negative value is ok here
            need_dst_truncate = src_stat_buffer.st_size < dst_stat_buffer.st_size;
        };
        const auto setup_append = [&]{
            dst_open_flags = O_WRONLY;
            do_unlink_on_stop = false;
            do_copy_xattrs = false;
            do_set_times = false;
            do_set_unix_flags = false;
            dst_size_on_stop = dst_stat_buffer.st_size;
            total_dst_size += dst_stat_buffer.st_size;
            initial_writing_offset = dst_stat_buffer.st_size;
            preallocate_delta = src_stat_buffer.st_size;
        };
        
        auto action = m_Options.exist_behavior;
        if( action == FileCopyOperationOptions::ExistBehavior::Ask )
            if( auto b = DialogResultToExistBehavior( m_OnCopyDestinationAlreadyExists(src_stat_buffer, dst_stat_buffer, _dst_path) ) )
                action = *b;
        
        switch( action ) {
            case FileCopyOperationOptions::ExistBehavior::SkipAll:      return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteOld: if( src_stat_buffer.st_mtime <= dst_stat_buffer.st_mtime ) return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteAll: setup_overwrite(); break;
            case FileCopyOperationOptions::ExistBehavior::AppendAll:    setup_append(); break;
            default:                                                    return StepResult::Stop;
        }
    }
    else {
        // no dest file - just create it
        dst_open_flags = O_WRONLY|O_CREAT;
        do_unlink_on_stop = true;
        dst_size_on_stop = 0;
        preallocate_delta = src_stat_buffer.st_size;
    }
    
    // open file descriptor for destination
    int destination_fd = -1;
    
    while( true ) {
        // we want to copy src permissions if options say so or just put default ones
        mode_t open_mode = m_Options.copy_unix_flags ? src_stat_buffer.st_mode : S_IRUSR | S_IWUSR | S_IRGRP;
        mode_t old_umask = umask( 0 );
        destination_fd = io.open( _dst_path.c_str(), dst_open_flags, open_mode );
        umask(old_umask);

        if( destination_fd != -1 )
            break; // we're good to go
        
        // failed to open destination file
        if( m_SkipAll )
            return StepResult::Skipped;
        
        switch( m_OnCantOpenDestinationFile(VFSError::FromErrno(), _dst_path) ) {
            case FileCopyOperationDR::Retry:      continue;
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            default:                              return StepResult::Stop;
        }
    }
    
    // don't forget ot close destination file descriptor anyway
    auto close_destination = at_scope_end([&]{
        if(destination_fd != -1) {
            close(destination_fd);
            destination_fd = -1;
        }
    });
    
    // for some circumstances we have to clean up remains if anything goes wrong
    // and do it BEFORE close_destination fires
    auto clean_destination = at_scope_end([&]{
        if( destination_fd != -1 ) {
            // we need to revert what we've done
            ftruncate(destination_fd, dst_size_on_stop);
            close(destination_fd);
            destination_fd = -1;
            if( do_unlink_on_stop )
                io.unlink( _dst_path.c_str() );
        }
    });
    
    // caching is meaningless here
    fcntl( destination_fd, F_NOCACHE, 1 );
    
    // find fs info for destination file.
    auto dst_fs_info_holder = NativeFSManager::Instance().VolumeFromFD( destination_fd );
    if( !dst_fs_info_holder )
        return StepResult::Stop; // something VERY BAD has happened, can't go on
    auto &dst_fs_info = *dst_fs_info_holder;
    
    if( ShouldPreallocateSpace(preallocate_delta, dst_fs_info) ) {
        // tell systme to preallocate space for data since we dont want to trash our disk
        PreallocateSpace(preallocate_delta, destination_fd);
        
        // truncate is needed for actual preallocation
        need_dst_truncate = true;
    }
    
    // set right size for destination file for preallocating itself and for reducing file size if necessary
    if( need_dst_truncate )
        while( ftruncate(destination_fd, total_dst_size) == -1 ) {
            // failed to set dest file size
            if(m_SkipAll)
                return StepResult::Skipped;
            
            switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
                case FileCopyOperationDR::Retry:      continue;
                case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                default:                                return StepResult::Stop;
            }
        }
    
    // find the right position in destination file
    if( initial_writing_offset > 0 ) {
        while( lseek(destination_fd, initial_writing_offset, SEEK_SET) == -1  ) {
            // failed seek in a file. lolwut?
            if(m_SkipAll)
                return StepResult::Skipped;
            
            switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
                case FileCopyOperationDR::Retry:      continue;
                case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                default:                                return StepResult::Stop;
            }
        }
    }
    
    
    
    auto read_buffer = m_Buffers[0].get(), write_buffer = m_Buffers[1].get();
    const uint32_t src_preffered_io_size = src_fs_info.basic.io_size < m_BufferSize ? src_fs_info.basic.io_size : m_BufferSize;
    const uint32_t dst_preffered_io_size = dst_fs_info.basic.io_size < m_BufferSize ? dst_fs_info.basic.io_size : m_BufferSize;
    constexpr int max_io_loops = 5; // looked in Apple's copyfile() - treat 5 zero-resulting reads/writes as an error
    uint32_t bytes_to_write = 0;
    uint64_t source_bytes_read = 0;
    uint64_t destination_bytes_written = 0;
    
    // read from source within current thread and write to destination within secondary queue
    while( src_stat_buffer.st_size != destination_bytes_written ) {
        
        // check user decided to pause operation or discard it
        if( CheckPauseOrStop() )
            return StepResult::Stop;
        
        // <<<--- writing in secondary thread --->>>
        optional<StepResult> write_return; // optional storage for error returning
        m_IOGroup.Run([this, bytes_to_write, destination_fd, write_buffer, dst_preffered_io_size, &destination_bytes_written, &write_return, &_dst_path]{
            uint32_t left_to_write = bytes_to_write;
            uint32_t has_written = 0; // amount of bytes written into destination this time
            int write_loops = 0;
            while( left_to_write > 0 ) {
                int64_t n_written = write(destination_fd, write_buffer + has_written, min(left_to_write, dst_preffered_io_size) );
                if( n_written > 0 ) {
                    has_written += n_written;
                    left_to_write -= n_written;
                    destination_bytes_written += n_written;
                }
                else if( n_written < 0 || (++write_loops > max_io_loops) ) {
                    if(m_SkipAll) {
                        write_return = StepResult::Skipped;
                        return;
                    }
                    switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
                        case FileCopyOperationDR::Retry:      continue;
                        case FileCopyOperationDR::Skip:       write_return = StepResult::Skipped; return;
                        case FileCopyOperationDR::SkipAll:    write_return = StepResult::SkipAll; return;
                        default:                                write_return = StepResult::Stop; return;
                    }
                }
            }
        });
        
        // <<<--- reading in current thread --->>>
        // here we handle the case in which source io size is much smaller than dest's io size
        uint32_t to_read = max( src_preffered_io_size, dst_preffered_io_size );
        if( src_stat_buffer.st_size - source_bytes_read < to_read )
            to_read = uint32_t(src_stat_buffer.st_size - source_bytes_read);
        uint32_t has_read = 0; // amount of bytes read into buffer this time
        int read_loops = 0; // amount of zero-resulting reads
        optional<StepResult> read_return; // optional storage for error returning
        while( to_read != 0 ) {
            int64_t read_result = read(source_fd, read_buffer + has_read, src_preffered_io_size);
            if( read_result > 0 ) {
                if(_source_data_feedback)
                    _source_data_feedback(read_buffer + has_read, (unsigned)read_result);
                source_bytes_read += read_result;
                has_read += read_result;
                to_read -= read_result;
            }
            else if( (read_result < 0) || (++read_loops > max_io_loops) ) {
                if(m_SkipAll) {
                    read_return = StepResult::Skipped;
                    break;
                }
                switch( m_OnSourceFileReadError(VFSError::FromErrno(), _src_path) ) {
                    case FileCopyOperationDR::Retry:      continue;
                    case FileCopyOperationDR::Skip:       read_return = StepResult::Skipped; break;
                    case FileCopyOperationDR::SkipAll:    read_return = StepResult::SkipAll; break;
                    default:                                read_return = StepResult::Stop; break;
                }
                break;
            }
        }
        
        m_IOGroup.Wait();
        
        // if something bad happened in reading or writing - return from this routine
        if( write_return )
            return *write_return;
        if( read_return )
            return *read_return;

        // update statistics
        m_Stats.AddValue( bytes_to_write );
        
        // swap buffers ang go again
        bytes_to_write = has_read;
        swap( read_buffer, write_buffer );
    }
    
    // we're ok, turn off destination cleaning
    clean_destination.disengage();
    
    // do xattr things
    // crazy OSX stuff: setting some xattrs like FinderInfo may actually change file's BSD flags
    if( m_Options.copy_xattrs  ) {
        if(do_erase_xattrs) // erase destination's xattrs
            EraseXattrsFromNativeFD(destination_fd);

        if(do_copy_xattrs) // copy xattrs from src to dest
            CopyXattrsFromNativeFDToNativeFD(source_fd, destination_fd);
    }

    // do flags things
    if( m_Options.copy_unix_flags && do_set_unix_flags ) {
        if(io.isrouted()) // long path
            io.chflags(_dst_path.c_str(), src_stat_buffer.st_flags);
        else
            fchflags(destination_fd, src_stat_buffer.st_flags);
    }

    // do times things
    if( m_Options.copy_file_times && do_set_times )
        AdjustFileTimesForNativeFD(destination_fd, src_stat_buffer);
    
    // do ownage things
    // TODO: we actually can't chown without superuser rights.
    // need to optimize this (sometimes) meaningless call
    if( m_Options.copy_unix_owners ) {
        if( io.isrouted() ) // long path
            io.chown(_dst_path.c_str(), src_stat_buffer.st_uid, src_stat_buffer.st_gid);
        else // short path
            fchown(destination_fd, src_stat_buffer.st_uid, src_stat_buffer.st_gid);
    }
    
    return StepResult::Ok;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// vfs file -> native file copying routine
//////////////////////////////////////////////////////////////////////////////////////////////////////////
FileCopyOperationJob::StepResult FileCopyOperationJob::CopyVFSFileToNativeFile(VFSHost &_src_vfs,
                                                                                     const string& _src_path,
                                                                                     const string& _dst_path,
                                                                                     function<void(const void *_data, unsigned _sz)> _source_data_feedback // will be used for checksum calculation for copying verifiyng
                                                                                    ) const
{
    auto &io = RoutedIO::Default;
    int ret = 0;
    
    // get information about source file
    VFSStat src_stat_buffer;
    while( (ret = _src_vfs.Stat(_src_path.c_str(), src_stat_buffer, 0, nullptr)) < 0 ) {
        // failed to stat source
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }

    // create source file object
    VFSFilePtr src_file;
    while( (ret = _src_vfs.CreateFile(_src_path.c_str(), src_file)) < 0 ) {
        // failed to create file object
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }

    // open source file
    while( (ret = src_file->Open(VFSFlags::OF_Read | VFSFlags::OF_ShLock | VFSFlags::OF_NoCache)) < 0 ) {
        // failed to open source file
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }
    
    
    // setting up copying scenario
    int     dst_open_flags          = 0;
    bool    do_erase_xattrs         = false,
            do_copy_xattrs          = true,
            do_unlink_on_stop       = false,
            do_set_times            = true,
            do_set_unix_flags       = true,
            need_dst_truncate       = false,
            dst_existed_before      = false;
    int64_t dst_size_on_stop        = 0,
            total_dst_size          = src_stat_buffer.size,
            preallocate_delta       = 0,
            initial_writing_offset  = 0;
    
    // stat destination
    struct stat dst_stat_buffer;
    if( io.stat(_dst_path.c_str(), &dst_stat_buffer) != -1 ) {
        // file already exist. what should we do now?
        dst_existed_before = true;
        const auto setup_overwrite = [&]{
            dst_open_flags = O_WRONLY;
            do_unlink_on_stop = true;
            dst_size_on_stop = 0;
            do_erase_xattrs = true;
            preallocate_delta = src_stat_buffer.size - dst_stat_buffer.st_size; // negative value is ok here
            need_dst_truncate = src_stat_buffer.size < dst_stat_buffer.st_size;
        };
        const auto setup_append = [&]{
            dst_open_flags = O_WRONLY;
            do_unlink_on_stop = false;
            do_copy_xattrs = false;
            do_set_times = false;
            do_set_unix_flags = false;
            dst_size_on_stop = dst_stat_buffer.st_size;
            total_dst_size += dst_stat_buffer.st_size;
            initial_writing_offset = dst_stat_buffer.st_size;
            preallocate_delta = src_stat_buffer.size;
        };
        
        auto action = m_Options.exist_behavior;
        if( action == FileCopyOperationOptions::ExistBehavior::Ask )
            if( auto b = DialogResultToExistBehavior( m_OnCopyDestinationAlreadyExists(src_stat_buffer.SysStat(), dst_stat_buffer, _dst_path) ) )
                action = *b;
        
        switch( action ) {
            case FileCopyOperationOptions::ExistBehavior::SkipAll:      return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteOld: if( src_stat_buffer.mtime.tv_sec <= dst_stat_buffer.st_mtime ) return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteAll: setup_overwrite(); break;
            case FileCopyOperationOptions::ExistBehavior::AppendAll:    setup_append(); break;
            default:                                                    return StepResult::Stop;
        }
    }
    else {
        // no dest file - just create it
        dst_open_flags = O_WRONLY|O_CREAT;
        do_unlink_on_stop = true;
        dst_size_on_stop = 0;
        preallocate_delta = src_stat_buffer.size;
    }
    
    // open file descriptor for destination
    int destination_fd = -1;
    
    while( true ) {
        // we want to copy src permissions if options say so or just put default ones
        mode_t open_mode = m_Options.copy_unix_flags ? src_stat_buffer.mode : S_IRUSR | S_IWUSR | S_IRGRP;
        mode_t old_umask = umask( 0 );
        destination_fd = io.open( _dst_path.c_str(), dst_open_flags, open_mode );
        umask(old_umask);
        
        if( destination_fd != -1 )
            break; // we're good to go
        
        // failed to open destination file
        if( m_SkipAll )
            return StepResult::Skipped;
        
        switch( m_OnCantOpenDestinationFile(VFSError::FromErrno(), _dst_path) ) {
            case FileCopyOperationDR::Retry:      continue;
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            default:                                return StepResult::Stop;
        }
    }
    
    // don't forget ot close destination file descriptor anyway
    auto close_destination = at_scope_end([&]{
        if(destination_fd != -1) {
            close(destination_fd);
            destination_fd = -1;
        }
    });
    
    // for some circumstances we have to clean up remains if anything goes wrong
    // and do it BEFORE close_destination fires
    auto clean_destination = at_scope_end([&]{
        if( destination_fd != -1 ) {
            // we need to revert what we've done
            ftruncate(destination_fd, dst_size_on_stop);
            close(destination_fd);
            destination_fd = -1;
            if( do_unlink_on_stop )
                io.unlink( _dst_path.c_str() );
        }
    });
    
    // caching is meaningless here
    fcntl( destination_fd, F_NOCACHE, 1 );
    
    // find fs info for destination file.
    auto dst_fs_info_holder = NativeFSManager::Instance().VolumeFromFD( destination_fd );
    if( !dst_fs_info_holder )
        return StepResult::Stop; // something VERY BAD has happened, can't go on
    auto &dst_fs_info = *dst_fs_info_holder;
    
    if( ShouldPreallocateSpace(preallocate_delta, dst_fs_info) ) {
        // tell systme to preallocate space for data since we dont want to trash our disk
        PreallocateSpace(preallocate_delta, destination_fd);
        
        // truncate is needed for actual preallocation
        need_dst_truncate = true;
    }
    
    // set right size for destination file for preallocating itself and for reducing file size if necessary
    if( need_dst_truncate )
        while( ftruncate(destination_fd, total_dst_size) == -1 ) {
            // failed to set dest file size
            if(m_SkipAll)
                return StepResult::Skipped;
            
            switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
                case FileCopyOperationDR::Retry:      continue;
                case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                default:                                return StepResult::Stop;
            }
        }
    
    // find the right position in destination file
    if( initial_writing_offset > 0 ) {
        while( lseek(destination_fd, initial_writing_offset, SEEK_SET) == -1  ) {
            // failed seek in a file. lolwut?
            if(m_SkipAll)
                return StepResult::Skipped;
            
            switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
                case FileCopyOperationDR::Retry:      continue;
                case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                default:                                return StepResult::Stop;
            }
        }
    }
    
    auto read_buffer = m_Buffers[0].get(), write_buffer = m_Buffers[1].get();
    const uint32_t dst_preffered_io_size = dst_fs_info.basic.io_size < m_BufferSize ? dst_fs_info.basic.io_size : m_BufferSize;
    const uint32_t src_preffered_io_size = dst_preffered_io_size; // not sure if this is a good idea, but seems to be ok
    constexpr int max_io_loops = 5; // looked in Apple's copyfile() - treat 5 zero-resulting reads/writes as an error
    uint32_t bytes_to_write = 0;
    uint64_t source_bytes_read = 0;
    uint64_t destination_bytes_written = 0;
    
    // read from source within current thread and write to destination within secondary queue
    while( src_stat_buffer.size != destination_bytes_written ) {
        
        // check user decided to pause operation or discard it
        if( CheckPauseOrStop() )
            return StepResult::Stop;
        
        // <<<--- writing in secondary thread --->>>
        optional<StepResult> write_return; // optional storage for error returning
        m_IOGroup.Run([this, bytes_to_write, destination_fd, write_buffer, dst_preffered_io_size, &destination_bytes_written, &write_return, &_dst_path]{
            uint32_t left_to_write = bytes_to_write;
            uint32_t has_written = 0; // amount of bytes written into destination this time
            int write_loops = 0;
            while( left_to_write > 0 ) {
                int64_t n_written = write(destination_fd, write_buffer + has_written, min(left_to_write, dst_preffered_io_size) );
                if( n_written > 0 ) {
                    has_written += n_written;
                    left_to_write -= n_written;
                    destination_bytes_written += n_written;
                }
                else if( n_written < 0 || (++write_loops > max_io_loops) ) {
                    if(m_SkipAll) {
                        write_return = StepResult::Skipped;
                        return;
                    }
                    switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
                        case FileCopyOperationDR::Retry:      continue;
                        case FileCopyOperationDR::Skip:       write_return = StepResult::Skipped; return;
                        case FileCopyOperationDR::SkipAll:    write_return = StepResult::SkipAll; return;
                        default:                                write_return = StepResult::Stop; return;
                    }
                }
            }
        });
        
        // <<<--- reading in current thread --->>>
        // here we handle the case in which source io size is much smaller than dest's io size
        uint32_t to_read = max( src_preffered_io_size, dst_preffered_io_size );
        if( src_stat_buffer.size - source_bytes_read < to_read )
            to_read = uint32_t(src_stat_buffer.size - source_bytes_read);
        uint32_t has_read = 0; // amount of bytes read into buffer this time
        int read_loops = 0; // amount of zero-resulting reads
        optional<StepResult> read_return; // optional storage for error returning
        while( to_read != 0 ) {
            int64_t read_result = src_file->Read(read_buffer + has_read, min(to_read, src_preffered_io_size));
            if( read_result > 0 ) {
                if(_source_data_feedback)
                    _source_data_feedback(read_buffer + has_read, (unsigned)read_result);
                source_bytes_read += read_result;
                has_read += read_result;
                assert( to_read >= read_result ); // regression assert
                to_read -= read_result;
            }
            else if( (read_result < 0) || (++read_loops > max_io_loops) ) {
                if(m_SkipAll) {
                    read_return = StepResult::Skipped;
                    break;
                }
                switch( m_OnSourceFileReadError((int)read_result, _src_path) ) {
                    case FileCopyOperationDR::Retry:      continue;
                    case FileCopyOperationDR::Skip:       read_return = StepResult::Skipped; break;
                    case FileCopyOperationDR::SkipAll:    read_return = StepResult::SkipAll; break;
                    default:                                read_return = StepResult::Stop; break;
                }
                break;
            }
        }
        
        m_IOGroup.Wait();
        
        // if something bad happened in reading or writing - return from this routine
        if( write_return )
            return *write_return;
        if( read_return )
            return *read_return;
        
        // update statistics
        m_Stats.AddValue( bytes_to_write );
        
        // swap buffers ang go again
        bytes_to_write = has_read;
        swap( read_buffer, write_buffer );
    }
    
    // we're ok, turn off destination cleaning
    clean_destination.disengage();
    
    // erase destination's xattrs
    if(m_Options.copy_xattrs && do_erase_xattrs)
        EraseXattrsFromNativeFD(destination_fd);
    
    // copy xattrs from src to dst
    if( m_Options.copy_xattrs && src_file->XAttrCount() > 0 )
        CopyXattrsFromVFSFileToNativeFD(*src_file, destination_fd);
    
    // adjust destination time as source
    if(m_Options.copy_file_times && do_set_times)
        AdjustFileTimesForNativeFD(destination_fd, src_stat_buffer);
    
    // change flags
    if( m_Options.copy_unix_flags && src_stat_buffer.meaning.flags ) {
        if(io.isrouted()) // long path
            io.chflags(_dst_path.c_str(), src_stat_buffer.flags);
        else
            fchflags(destination_fd, src_stat_buffer.flags);
    }
    
    // change ownage
    if(m_Options.copy_unix_owners) {
        if(io.isrouted()) // long path
            io.chown(_dst_path.c_str(), src_stat_buffer.uid, src_stat_buffer.gid);
        else
            fchown(destination_fd, src_stat_buffer.uid, src_stat_buffer.gid);
    }
    
    return StepResult::Ok;
}


FileCopyOperationJob::StepResult FileCopyOperationJob::CopyVFSFileToVFSFile(VFSHost &_src_vfs,
                                                                                  const string& _src_path,
                                                                                  const string& _dst_path,
                                                                                  function<void(const void *_data, unsigned _sz)> _source_data_feedback // will be used for checksum calculation for copying verifiyng
                                                                                    ) const
{
    int ret = 0;
    
    // get information about source file
    VFSStat src_stat_buffer;
    while( (ret = _src_vfs.Stat(_src_path.c_str(), src_stat_buffer, 0, nullptr)) < 0 ) {
        // failed to stat source
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }
    
    // create source file object
    VFSFilePtr src_file;
    while( (ret = _src_vfs.CreateFile(_src_path.c_str(), src_file)) < 0 ) {
        // failed to create file object
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }
    
    // open source file
    while( (ret = src_file->Open(VFSFlags::OF_Read | VFSFlags::OF_ShLock | VFSFlags::OF_NoCache)) < 0 ) {
        // failed to open source file
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:       return StepResult::Stop;
        }
    }
    
    
    // setting up copying scenario
    int     dst_open_flags          = 0;
    bool    do_erase_xattrs         = false,
            do_copy_xattrs          = true,
            do_unlink_on_stop       = false,
            do_set_times            = true,
            do_set_unix_flags       = true,
            need_dst_truncate       = false,
            dst_existed_before      = false;
            int64_t dst_size_on_stop= 0,
            total_dst_size          = src_stat_buffer.size,
            initial_writing_offset  = 0;
    
    // stat destination
    VFSStat dst_stat_buffer;
    if( m_DestinationHost->Stat(_dst_path.c_str(), dst_stat_buffer, 0, 0) == 0) {
        // file already exist. what should we do now?
        dst_existed_before = true;
        const auto setup_overwrite = [&]{
            dst_open_flags = VFSFlags::OF_Write | VFSFlags::OF_Truncate | VFSFlags::OF_NoCache;
            do_unlink_on_stop = true;
            dst_size_on_stop = 0;
            do_erase_xattrs = true;
            need_dst_truncate = src_stat_buffer.size < dst_stat_buffer.size;
        };
        const auto setup_append = [&]{
            dst_open_flags = VFSFlags::OF_Write | VFSFlags::OF_Append | VFSFlags::OF_NoCache;
            do_unlink_on_stop = false;
            do_copy_xattrs = false;
            do_set_times = false;
            do_set_unix_flags = false;
            dst_size_on_stop = dst_stat_buffer.size;
            total_dst_size += dst_stat_buffer.size;
            initial_writing_offset = dst_stat_buffer.size;
        };
        
        auto action = m_Options.exist_behavior;
        if( action == FileCopyOperationOptions::ExistBehavior::Ask )
            if( auto b = DialogResultToExistBehavior( m_OnCopyDestinationAlreadyExists(src_stat_buffer.SysStat(), dst_stat_buffer.SysStat(), _dst_path) ) )
                action = *b;
        
        switch( action ) {
            case FileCopyOperationOptions::ExistBehavior::SkipAll:      return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteOld: if( src_stat_buffer.mtime.tv_sec <= dst_stat_buffer.mtime.tv_sec ) return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteAll: setup_overwrite(); break;
            case FileCopyOperationOptions::ExistBehavior::AppendAll:    setup_append(); break;
            default:                                                    return StepResult::Stop;
        }
    }
    else {
        // no dest file - just create it
        dst_open_flags = VFSFlags::OF_Write | VFSFlags::OF_Create | VFSFlags::OF_NoCache;
        do_unlink_on_stop = true;
        dst_size_on_stop = 0;
    }
    
    // open file object for destination
    VFSFilePtr dst_file;
    while( (ret = m_DestinationHost->CreateFile(_dst_path.c_str(), dst_file)) != 0 ) {
        // failed to create destination file
        if( m_SkipAll )
            return StepResult::Skipped;
        
        switch( m_OnCantOpenDestinationFile(ret, _dst_path) ) {
            case FileCopyOperationDR::Retry:      continue;
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            default:                                return StepResult::Stop;
        }
        
    }
    
    // open file itself
    while( true ) {
        dst_open_flags |= m_Options.copy_unix_flags ?
            (src_stat_buffer.mode & (S_IRWXU | S_IRWXG | S_IRWXO)) :
            (S_IRUSR | S_IWUSR | S_IRGRP);
        
        ret = dst_file->Open(dst_open_flags);
        if( ret == 0 )
            break; // ok
        
        // failed to open dest file
        if( m_SkipAll )
            return StepResult::Skipped;
        
        switch( m_OnCantOpenDestinationFile(ret, _dst_path) ) {
            case FileCopyOperationDR::Retry:      continue;
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            default:                                return StepResult::Stop;
        }
    }
    

    
    // for some circumstances we have to clean up remains if anything goes wrong
    // and do it BEFORE close_destination fires
    auto clean_destination = at_scope_end([&]{
        if( dst_file && dst_file->IsOpened() ) {
            // we need to revert what we've done
            dst_file->Close();
            dst_file.reset();
            if( do_unlink_on_stop == true )
                m_DestinationHost->Unlink(_dst_path.c_str(), 0);
        }
    });

    // tell upload-only vfs'es how much we're going to write
    dst_file->SetUploadSize( src_stat_buffer.size );
    
    // find the right position in destination file
    if( dst_file->Pos() != initial_writing_offset ) {
        auto seek_ret = dst_file->Seek(initial_writing_offset, VFSFile::Seek_Set);
        if(seek_ret < 0)
            return StepResult::Skipped; // BAD!
    }
    
    auto read_buffer = m_Buffers[0].get(), write_buffer = m_Buffers[1].get();
    const uint32_t dst_preffered_io_size = m_BufferSize;
    const uint32_t src_preffered_io_size = m_BufferSize;
    constexpr int max_io_loops = 5; // looked in Apple's copyfile() - treat 5 zero-resulting reads/writes as an error
    uint32_t bytes_to_write = 0;
    uint64_t source_bytes_read = 0;
    uint64_t destination_bytes_written = 0;
    
    // read from source within current thread and write to destination within secondary queue
    while( src_stat_buffer.size != destination_bytes_written ) {
        
        // check user decided to pause operation or discard it
        if( CheckPauseOrStop() )
            return StepResult::Stop;
        
        // <<<--- writing in secondary thread --->>>
        optional<StepResult> write_return; // optional storage for error returning
        m_IOGroup.Run([this, bytes_to_write, &dst_file, write_buffer, dst_preffered_io_size, &destination_bytes_written, &write_return, &_dst_path]{
            uint32_t left_to_write = bytes_to_write;
            uint32_t has_written = 0; // amount of bytes written into destination this time
            int write_loops = 0;
            while( left_to_write > 0 ) {
//                int64_t n_written = write(destination_fd, write_buffer + has_written, min(left_to_write, dst_preffered_io_size) );
                int64_t n_written = dst_file->Write( write_buffer + has_written, min(left_to_write, dst_preffered_io_size) );
                if( n_written > 0 ) {
                    has_written += n_written;
                    left_to_write -= n_written;
                    destination_bytes_written += n_written;
                }
                else if( n_written < 0 || (++write_loops > max_io_loops) ) {
                    if(m_SkipAll) {
                        write_return = StepResult::Skipped;
                        return;
                    }
                    switch( m_OnDestinationFileWriteError((int)n_written, _dst_path) ) {
                        case FileCopyOperationDR::Retry:      continue;
                        case FileCopyOperationDR::Skip:       write_return = StepResult::Skipped; return;
                        case FileCopyOperationDR::SkipAll:    write_return = StepResult::SkipAll; return;
                        default:                                write_return = StepResult::Stop; return;
                    }
                }
            }
        });
        
        // <<<--- reading in current thread --->>>
        // here we handle the case in which source io size is much smaller than dest's io size
        uint32_t to_read = max( src_preffered_io_size, dst_preffered_io_size );
        if( src_stat_buffer.size - source_bytes_read < to_read )
            to_read = uint32_t(src_stat_buffer.size - source_bytes_read);
        uint32_t has_read = 0; // amount of bytes read into buffer this time
        int read_loops = 0; // amount of zero-resulting reads
        optional<StepResult> read_return; // optional storage for error returning
        while( to_read != 0 ) {
            int64_t read_result =  src_file->Read(read_buffer + has_read, min(to_read, src_preffered_io_size));
            if( read_result > 0 ) {
                if(_source_data_feedback)
                    _source_data_feedback(read_buffer + has_read, (unsigned)read_result);
                source_bytes_read += read_result;
                has_read += read_result;
                to_read -= read_result;
            }
            else if( (read_result < 0) || (++read_loops > max_io_loops) ) {
                if(m_SkipAll) {
                    read_return = StepResult::Skipped;
                    break;
                }
                switch( m_OnSourceFileReadError((int)read_result, _src_path) ) {
                    case FileCopyOperationDR::Retry:      continue;
                    case FileCopyOperationDR::Skip:       read_return = StepResult::Skipped; break;
                    case FileCopyOperationDR::SkipAll:    read_return = StepResult::SkipAll; break;
                    default:                                read_return = StepResult::Stop; break;
                }
                break;
            }
        }
        
        m_IOGroup.Wait();
        
        // if something bad happened in reading or writing - return from this routine
        if( write_return )
            return *write_return;
        if( read_return )
            return *read_return;
        
        // update statistics
        m_Stats.AddValue( bytes_to_write );
        
        // swap buffers ang go again
        bytes_to_write = has_read;
        swap( read_buffer, write_buffer );
    }
    
    // we're ok, turn off destination cleaning
    clean_destination.disengage();
    
    
    // TODO:
    // xattrs
    // owners
    // flags
    // file times
    
    return StepResult::Ok;
}

// uses m_Buffer[0] to reduce mallocs
// currently there's no error handling or reporting here. may need this in the future. maybe.
void FileCopyOperationJob::EraseXattrsFromNativeFD(int _fd_in) const
{
    auto xnames = (char*)m_Buffers[0].get();
    auto xnamesizes = flistxattr(_fd_in, xnames, m_BufferSize, 0);
    for( auto s = xnames, e = xnames + xnamesizes; s < e; s += strlen(s) + 1 ) // iterate thru xattr names..
        fremovexattr(_fd_in, s, 0); // ..and remove everyone
}

// uses m_Buffer[0] and m_Buffer[1] to reduce mallocs
// currently there's no error handling or reporting here. may need this in the future. maybe.
void FileCopyOperationJob::CopyXattrsFromNativeFDToNativeFD(int _fd_from, int _fd_to) const
{
    auto xnames = (char*)m_Buffers[0].get();
    auto xdata = m_Buffers[1].get();
    auto xnamesizes = flistxattr(_fd_from, xnames, m_BufferSize, 0);
    for( auto s = xnames, e = xnames + xnamesizes; s < e; s += strlen(s) + 1 ) { // iterate thru xattr names..
        auto xattrsize = fgetxattr(_fd_from, s, xdata, m_BufferSize, 0, 0); // and read all these xattrs
        if( xattrsize >= 0 ) // xattr can be zero-length, just a tag itself
            fsetxattr(_fd_to, s, xdata, xattrsize, 0, 0); // write them into _fd_to
    }
}

void FileCopyOperationJob::CopyXattrsFromVFSFileToNativeFD(VFSFile& _source, int _fd_to) const
{
    auto buf = m_Buffers[0].get();
    size_t buf_sz = m_BufferSize;
    _source.XAttrIterateNames([&](const char *name){
        ssize_t res = _source.XAttrGet(name, buf, buf_sz);
        if(res >= 0)
            fsetxattr(_fd_to, name, buf, res, 0, 0);
        return true;
    });
}

void FileCopyOperationJob::CopyXattrsFromVFSFileToPath(VFSFile& _file, const char *_fn_to) const
{
    auto buf = m_Buffers[0].get();
    size_t buf_sz = m_BufferSize;
    
    _file.XAttrIterateNames(^bool(const char *name){
        ssize_t res = _file.XAttrGet(name, buf, buf_sz);
        if(res >= 0)
            setxattr(_fn_to, name, buf, res, 0, 0);
        return true;
    });
}

FileCopyOperationJob::StepResult FileCopyOperationJob::CopyNativeDirectoryToNativeDirectory(const string& _src_path,
                                                                                                  const string& _dst_path) const
{
    auto &io = RoutedIO::Default;
    
    struct stat src_stat_buf;
    if( io.stat(_dst_path.c_str(), &src_stat_buf) != -1 ) {
        // target already exists
        
        if( !S_ISDIR(src_stat_buf.st_mode) ) {
            // ouch - existing entry is not a directory
            // TODO: ask user about this and remove this entry if he agrees
            return StepResult::Ok;
        }
    }
    else {
        // create target directory
        constexpr mode_t new_dir_mode = S_IXUSR|S_IXGRP|S_IXOTH|S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR;
        while( io.mkdir(_dst_path.c_str(), new_dir_mode) == -1  ) {
            // failed to create a directory
            if(m_SkipAll)
                return StepResult::Skipped;
            switch( m_OnCantCreateDestinationDir(VFSError::FromErrno(), _dst_path) ) {
                case FileCopyOperationDR::Retry:      continue;
                case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                default:                                return StepResult::Stop;
            }
        }
    }
    
    // do attributes stuff
    // we currently ignore possible errors on attributes copying, which is not great at all
    int src_fd = io.open(_src_path.c_str(), O_RDONLY);
    if( src_fd == -1 )
        return StepResult::Ok;
    auto clean_src_fd = at_scope_end([&]{ close(src_fd); });

    int dst_fd = io.open(_dst_path.c_str(), O_RDONLY); // strangely this works
    if( dst_fd == -1 )
        return StepResult::Ok;
    auto clean_dst_fd = at_scope_end([&]{ close(dst_fd); });
    
    struct stat src_stat;
    if( fstat(src_fd, &src_stat) != 0 )
        return StepResult::Ok;
    
    if(m_Options.copy_unix_flags) {
        // change unix mode
        fchmod(dst_fd, src_stat.st_mode);
        
        // change flags
        fchflags(dst_fd, src_stat.st_flags);
    }
    
    if(m_Options.copy_unix_owners) // change ownage
        io.chown(_dst_path.c_str(), src_stat.st_uid, src_stat.st_gid);
    
    if(m_Options.copy_xattrs) // copy xattrs
        CopyXattrsFromNativeFDToNativeFD(src_fd, dst_fd);
    
    if(m_Options.copy_file_times) // adjust destination times
        AdjustFileTimesForNativeFD(dst_fd, src_stat);
    
    return StepResult::Ok;
}

FileCopyOperationJob::StepResult FileCopyOperationJob::CopyVFSDirectoryToNativeDirectory(VFSHost &_src_vfs,
                                                                                               const string& _src_path,
                                                                                               const string& _dst_path) const
{
    auto &io = RoutedIO::Default;
    
    struct stat src_stat_buf;
    if( io.stat(_dst_path.c_str(), &src_stat_buf) != -1 ) {
        // target already exists
        
        if( !S_ISDIR(src_stat_buf.st_mode) ) {
            // ouch - existing entry is not a directory
            // TODO: ask user about this and remove this entry if he agrees
            return StepResult::Ok;
        }
    }
    else {
        // create target directory
        constexpr mode_t new_dir_mode = S_IXUSR|S_IXGRP|S_IXOTH|S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR;
        while( io.mkdir(_dst_path.c_str(), new_dir_mode) == -1  ) {
            // failed to create a directory
            if(m_SkipAll)
                return StepResult::Skipped;
            switch( m_OnCantCreateDestinationDir(VFSError::FromErrno(), _dst_path) ) {
                case FileCopyOperationDR::Retry:      continue;
                case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                default:                                return StepResult::Stop;
            }
        }
    }
    
    
    // do attributes stuff
    // we currently ignore possible errors on attributes copying, which is not great at all
    
    VFSStat src_stat_buffer;
    if( _src_vfs.Stat(_src_path.c_str(), src_stat_buffer, 0, 0) < 0 )
        return StepResult::Ok;
    
    if( m_Options.copy_file_times )
        AdjustFileTimesForNativePath( _dst_path.c_str(), src_stat_buffer );
    
    if(m_Options.copy_unix_flags) {
        // change unix mode
        mode_t mode = src_stat_buffer.mode;
        if( (mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == 0)
            mode |= S_IRWXU | S_IRGRP | S_IXGRP; // guard against malformed(?) archives
        io.chmod(_dst_path.c_str(), mode);
        
        // change flags
        if( src_stat_buffer.meaning.flags )
            io.chflags(_dst_path.c_str(), src_stat_buffer.flags);
    }
    
    // xattr processing
    if( m_Options.copy_xattrs ) {
        shared_ptr<VFSFile> src_file;
        if(_src_vfs.CreateFile(_src_path.c_str(), src_file, 0) >= 0)
            if( src_file->Open(VFSFlags::OF_Read | VFSFlags::OF_Directory | VFSFlags::OF_ShLock) >= 0 )
                if( src_file->XAttrCount() > 0 )
                    CopyXattrsFromVFSFileToPath(*src_file, _dst_path.c_str() );
    }
    
    return StepResult::Ok;
}

FileCopyOperationJob::StepResult FileCopyOperationJob::CopyVFSDirectoryToVFSDirectory(VFSHost &_src_vfs,
                                                                                            const string& _src_path,
                                                                                            const string& _dst_path) const
{
    int ret = 0;
    VFSStat src_st, dest_st;
    
    while( (ret = _src_vfs.Stat(_src_path.c_str(), src_st, 0, 0)) != 0) {
        // failed to stat source directory
        if(m_SkipAll)
            return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem(ret, _dst_path) ) {
            case FileCopyOperationDR::Retry:      continue;
            case FileCopyOperationDR::Skip:       return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
            default:                                return StepResult::Stop;
        }
        
        
    }

    if( m_DestinationHost->Stat( _dst_path.c_str(), dest_st, VFSFlags::F_NoFollow, 0) == 0) {
        // this directory already exist. currently do nothing, later - update it's attrs.
    }
    else {
        while( (ret = m_DestinationHost->CreateDirectory(_dst_path.c_str(), src_st.mode, 0)) != 0) {
            // failed to create a directory
            if(m_SkipAll)
                return StepResult::Skipped;
            switch( m_OnCantCreateDestinationDir(ret, _dst_path) ) {
                case FileCopyOperationDR::Retry:      continue;
                case FileCopyOperationDR::Skip:       return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:    return StepResult::SkipAll;
                default:                                return StepResult::Stop;
            }
        }
    }
    
    // no attrs currently
    
    return StepResult::Ok;
}

FileCopyOperationJob::StepResult FileCopyOperationJob::RenameNativeFile(const string& _src_path,
                                                                              const string& _dst_path) const
{
    auto &io = RoutedIO::Default;    
    
    // check if destination file already exist
    struct stat dst_stat_buffer;
    if( io.lstat(_dst_path.c_str(), &dst_stat_buffer) != -1 ) {
        // Destination file already exists.
        // Check if destination and source paths reference the same file. In this case,
        // silently rename the file.
        
        struct stat src_stat_buffer;
        while( io.lstat(_src_path.c_str(), &src_stat_buffer) == -1) {
            // failed to stat source
            if( m_SkipAll ) return StepResult::Skipped;
            switch( m_OnCantAccessSourceItem( VFSError::FromErrno(), _src_path ) ) {
                case FileCopyOperationDR::Retry:    continue;
                case FileCopyOperationDR::Skip:     return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
                case FileCopyOperationDR::Stop:     return StepResult::Stop;
                default:                            return StepResult::Stop;
            }
        }
        
        if( src_stat_buffer.st_dev != dst_stat_buffer.st_dev || // actually st_dev should ALWAYS be the same for this routine
            src_stat_buffer.st_ino != dst_stat_buffer.st_ino ) {
            // files are different, so renaming into _dst_path will erase it.
            // need to ask user what to do

            auto action = m_Options.exist_behavior;
            if( action == FileCopyOperationOptions::ExistBehavior::Ask )
                if( auto b = DialogResultToExistBehavior( m_OnRenameDestinationAlreadyExists(src_stat_buffer, dst_stat_buffer, _dst_path) ) )
                    action = *b;
            
            switch( action ) {
                case FileCopyOperationOptions::ExistBehavior::SkipAll:      return StepResult::Skipped;
                case FileCopyOperationOptions::ExistBehavior::OverwriteOld: if( src_stat_buffer.st_mtime <= dst_stat_buffer.st_mtime ) return StepResult::Skipped;
                case FileCopyOperationOptions::ExistBehavior::OverwriteAll: break;
                default:                                                    return StepResult::Stop;
            }
        }
    }
    
    // do rename itself
    while( io.rename(_src_path.c_str(), _dst_path.c_str()) == -1 ) {
        // failed to rename
        if( m_SkipAll ) return StepResult::Skipped;
        
        // ask user what to do
        switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            default:                            return StepResult::Stop;
        }
    }
    
    return StepResult::Ok;
}

FileCopyOperationJob::StepResult FileCopyOperationJob::RenameVFSFile(VFSHost &_common_host,
                                                                           const string& _src_path,
                                                                           const string& _dst_path) const
{
    // check if destination file already exist
    int ret = 0;
    
    VFSStat dst_stat_buffer;
    if( _common_host.Stat(_dst_path.c_str(), dst_stat_buffer, VFSFlags::F_NoFollow, nullptr) == 0 ) {
        // Destination file already exists.
        
        VFSStat src_stat_buffer;
        while( (ret = _common_host.Stat(_src_path.c_str(), src_stat_buffer, VFSFlags::F_NoFollow, nullptr)) != 0 ) {
            // failed to stat source
            if( m_SkipAll ) return StepResult::Skipped;
            switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
                case FileCopyOperationDR::Retry:    continue;
                case FileCopyOperationDR::Skip:     return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
                case FileCopyOperationDR::Stop:     return StepResult::Stop;
                default:                            return StepResult::Stop;
            }
        }
        
        // renaming into _dst_path will erase it. need to ask user what to do
        auto action = m_Options.exist_behavior;
        if( action == FileCopyOperationOptions::ExistBehavior::Ask )
            if( auto b = DialogResultToExistBehavior( m_OnRenameDestinationAlreadyExists(src_stat_buffer.SysStat(), dst_stat_buffer.SysStat(), _dst_path) ) )
                action = *b;
        
        switch( action ) {
            case FileCopyOperationOptions::ExistBehavior::SkipAll:      return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteOld: if( src_stat_buffer.mtime.tv_nsec <= dst_stat_buffer.mtime.tv_nsec ) return StepResult::Skipped;
            case FileCopyOperationOptions::ExistBehavior::OverwriteAll: break;
            default:                                                    return StepResult::Stop;
        }
    }

    // do rename itself
    while( (ret = _common_host.Rename(_src_path.c_str(), _dst_path.c_str())) != 0 ) {
    
        // failed to rename
        if( m_SkipAll ) return StepResult::Skipped;
        
        // ask user what to do
        switch( m_OnDestinationFileWriteError(ret, _dst_path) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            default:                            return StepResult::Stop;
        }
    }
    
    return StepResult::Ok;
}

void FileCopyOperationJob::CleanSourceItems() const
{
    for( auto i = rbegin(m_SourceItemsToDelete), e = rend(m_SourceItemsToDelete); i != e; ++i ) {
        auto index = *i;
        auto mode = m_SourceItems.ItemMode(index);
        auto&host = m_SourceItems.ItemHost(index);
        auto source_path = m_SourceItems.ComposeFullPath(index);
        
        // maybe any error handling here?
        if( S_ISDIR(mode) )
            host.RemoveDirectory( source_path.c_str() );
        else
            host.Unlink( source_path.c_str() );
    }
}

FileCopyOperationJob::StepResult FileCopyOperationJob::VerifyCopiedFile(const ChecksumExpectation& _exp, bool &_matched) const
{
    _matched = false;
    VFSFilePtr file;
    int rc;
    while( (rc = m_DestinationHost->CreateFile( _exp.destination_path.c_str(), file, nullptr )) != 0) {
        // failed to create dest file object
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnDestinationFileReadError( rc, _exp.destination_path ) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:     return StepResult::Stop;
            default:                            return StepResult::Stop;
        }
    }

    while( (rc = file->Open( VFSFlags::OF_Read | VFSFlags::OF_ShLock | VFSFlags::OF_NoCache )) != 0) {
        // failed to open dest file
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnDestinationFileReadError( rc, _exp.destination_path ) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:     return StepResult::Stop;
            default:                            return StepResult::Stop;
        }
    }
    
    Hash hash(Hash::MD5);
    
    uint64_t sz = file->Size();
    uint64_t szleft = sz;
    void *buf = m_Buffers[0].get();
    uint64_t buf_sz = m_BufferSize;

    while( szleft > 0 ) {
        // check user decided to pause operation or discard it
        if( CheckPauseOrStop() )
            return StepResult::Stop;
        ssize_t r = file->Read(buf, min(szleft, buf_sz));
        if(r < 0) {
            if( m_SkipAll ) return StepResult::Skipped;
            switch( m_OnDestinationFileReadError( (int)r, _exp.destination_path ) ) {
                case FileCopyOperationDR::Retry:    continue;
                case FileCopyOperationDR::Skip:     return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
                case FileCopyOperationDR::Stop:     return StepResult::Stop;
                default:                            return StepResult::Stop;
            }
        }
        else {
            szleft -= r;
            hash.Feed(buf, r);
        }
    }
    file->Close();

    auto md5 = hash.Final();
    bool checksum_match = equal(begin(md5), end(md5), begin(_exp.md5.buf));
    _matched = checksum_match;
    
    return StepResult::Ok;
}

FileCopyOperationJob::StepResult FileCopyOperationJob::CopyNativeSymlinkToNative(const string& _src_path,
                                                                                       const string& _dst_path) const
{
    auto &io = RoutedIO::Default;
    
    char linkpath[MAXPATHLEN];
    int result;
    ssize_t sz;
    
    while( (sz = io.readlink(_src_path.c_str(), linkpath, MAXPATHLEN)) == -1 ) {
        // failed to read symlink from source
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( VFSError::FromErrno(), _src_path ) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:     return StepResult::Stop;
            default:                            return StepResult::Stop;
        }
    }
    linkpath[sz] = 0;    
    
    while( (result = io.symlink(linkpath, _dst_path.c_str())) == -1 ) {
        // failed to create a symlink
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            default:                            return StepResult::Stop;
        }
    }
    
    return StepResult::Ok;
}

FileCopyOperationJob::StepResult FileCopyOperationJob::CopyVFSSymlinkToNative(VFSHost &_src_vfs,
                                                                                    const string& _src_path,
                                                                                    const string& _dst_path) const
{
    auto &io = RoutedIO::Default;
    
    char linkpath[MAXPATHLEN];
    int result;
    
    while( (result = _src_vfs.ReadSymlink(_src_path.c_str(), linkpath, MAXPATHLEN)) < 0 ) {
        // failed to read symlink from source
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( result, _src_path ) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            case FileCopyOperationDR::Stop:     return StepResult::Stop;
            default:                            return StepResult::Stop;
        }
    }
    
    while( (result = io.symlink(linkpath, _dst_path.c_str())) == -1 ) {
        // failed to create a symlink
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnDestinationFileWriteError(VFSError::FromErrno(), _dst_path) ) {
            case FileCopyOperationDR::Retry:    continue;
            case FileCopyOperationDR::Skip:     return StepResult::Skipped;
            case FileCopyOperationDR::SkipAll:  return StepResult::SkipAll;
            default:                            return StepResult::Stop;
        }
    }
    
    return StepResult::Ok;
}

void FileCopyOperationJob::SetState(FileCopyOperationJob::JobStage _state)
{
    NotifyWillChange(Notify::Stage);
    m_Stage = _state;
    NotifyDidChange(Notify::Stage);
}
