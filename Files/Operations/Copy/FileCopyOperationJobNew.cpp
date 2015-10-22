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

//#include <sys/sendfile.h>
//
//#include <copyfile.h>

#include "Common.h"

#include "VFS.h"
#include "RoutedIO.h"
#include "FileCopyOperationJobNew.h"
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

FileCopyOperationJobNew::ChecksumExpectation::ChecksumExpectation( int _source_ind, string _destination, const vector<uint8_t> &_md5 ):
    original_item( _source_ind ),
    destination_path( move(_destination) )
{
    copy(begin(_md5), end(_md5), begin(md5.buf)); // no buffer overflow check here
}

void FileCopyOperationJobNew::Init(vector<VFSFlexibleListingItem> _source_items,
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
    
}

void FileCopyOperationJobNew::Do()
{
    m_IsSingleItemProcessing = m_VFSListingItems.size() == 1;
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
    
    m_VFSListingItems.clear(); // don't need them anymore
    
    ProcessItems();
    
    SetCompleted();
}

void FileCopyOperationJobNew::ProcessItems()
{
    const bool dest_host_is_native = m_DestinationHost->IsNativeFS();
    auto is_same_native_volume = [this, &nfsm = NativeFSManager::Instance()]( int _index ) {
        return nfsm.VolumeFromDevID( m_SourceItems.ItemDev(_index) ) == m_DestinationNativeFSInfo;
    };
    
    for( int index = 0, index_end = m_SourceItems.ItemsAmount(); index != index_end; ++index ) {
        auto source_mode = m_SourceItems.ItemMode(index);
        auto&source_host = m_SourceItems.ItemHost(index);
        auto destination_path = ComposeDestinationNameForItem(index);
        auto source_path = m_SourceItems.ComposeFullPath(index);
        
        StepResult step_result = StepResult::Stop;
        
        if( S_ISREG(source_mode) ) {
            /////////////////////////////////////////////////////////////////////////////////////////////////
            // Regular files
            /////////////////////////////////////////////////////////////////////////////////////////////////
            optional<Hash> hash;
            auto hash_feedback = [&](const void *_data, unsigned _sz) {
                if( !hash ) hash.emplace(Hash::MD5);
                hash->Feed( _data, _sz );
            };
            function<void(const void *_data, unsigned _sz)> data_feedback = nullptr;
            
            if( source_host.IsNativeFS() && dest_host_is_native ) {
                // native fs processing
                if( m_Options.docopy ) {
                    if( m_Options.verification == ChecksumVerification::Always )
                        data_feedback = hash_feedback;
                    step_result = CopyNativeFileToNativeFile(source_path, destination_path, data_feedback);
                }
                else {
                    if( is_same_native_volume(index) ) { // rename
                        step_result = RenameNativeFile(source_path, destination_path);
                    }
                    else { // move
                        if( m_Options.verification >= ChecksumVerification::WhenMoves )
                            data_feedback = hash_feedback;
                        step_result = CopyNativeFileToNativeFile(source_path, destination_path, data_feedback);
                        if( step_result == StepResult::Ok )
                            m_SourceItemsToDelete.emplace_back(index); // mark source file for deletion
                    }
                }
            }
            else if( dest_host_is_native  ) {
                if( m_Options.docopy ) {
                    if( m_Options.verification == ChecksumVerification::Always )
                        data_feedback = hash_feedback;

//                    step_result = CopyNativeFileToNativeFile(source_path, destination_path, data_feedback);
                    step_result = CopyVFSFileToNativeFile(source_host, source_path, destination_path, data_feedback);
                    
                    
                }
                
            }
            else {
                if( m_Options.docopy ) {
                    if( m_Options.verification == ChecksumVerification::Always )
                        data_feedback = hash_feedback;
                    
                    step_result = CopyVFSFileToVFSFile(source_host, source_path, destination_path, data_feedback);
        
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
            if( source_host.IsNativeFS() && dest_host_is_native ) {
                // native fs processing
                if( m_Options.docopy ) {
                    step_result = CopyNativeDirectoryToNativeDirectory(source_path, destination_path);
                }
                else {
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
            else
                assert(0);
        }
//        else if( LINK )
        
        
        /// do something about step_result here
        if( step_result != StepResult::Ok )
            cout << source_path << " fucked up" << endl;
        
    }
  
    for( auto &item: m_Checksums ) {
        bool matched = false;
        auto step_result = VerifyCopiedFile(item, matched);
        cout << item.destination_path << " | " << (matched ? "checksum match" : "checksum mismatch") << endl;
    }
    
    // be sure to all it only if ALL previous steps wre OK.
    CleanSourceItems();
}

string FileCopyOperationJobNew::ComposeDestinationNameForItem( int _src_item_index ) const
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
        if( m_IsSingleItemProcessing ) {
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
        
        
        
//        // compose dest name
//        strcpy(destinationpath, m_Destination.c_str());
//        // here we need to find if user wanted just to copy a single top-level directory
//        // if so - don't touch destination name. otherwise - add an original path there
//        if(m_IsSingleEntryCopy) {
//            // for top level we need to just leave path without changes - skip top level's entry name
//            // for nested entries we need to cut first part of a path
//            if(strchr(_path, '/') != 0)
//                strcat(destinationpath, strchr(_path, '/'));
//        }
//
//        assert(0); // later
    }
}

void FileCopyOperationJobNew::test(string _from, string _to)
{
    CopyNativeFileToNativeFile(_from, _to, nullptr);
}

void FileCopyOperationJobNew::test2(string _dest, VFSHostPtr _host)
{
    m_InitialDestinationPath = _dest;
    m_DestinationHost = _host;
    bool need_to_build = false;
    auto comp_type = AnalyzeInitialDestination(m_DestinationPath, need_to_build);
    if( need_to_build )
        BuildDestinationDirectory();
    
    
    
    int a = 10;
}

void FileCopyOperationJobNew::Do_Hack()
{
    Do();
}

void FileCopyOperationJobNew::test3(string _dir, string _filename, VFSHostPtr _host)
{
    vector<VFSFlexibleListingItem> items;
    int ret = _host->FetchFlexibleListingItems(_dir, {_filename}, 0, items, nullptr);
    m_VFSListingItems = items;
    
    auto result = ScanSourceItems();

    
    int a = 10;
}

//static auto run_test = []{
    
//    for( int i = 0; i < 2; ++i ) {
//        FileCopyOperationJobNew job;
//        MachTimeBenchmark mtb;
//        job.test("/users/migun/1/bigfile.avi", "/users/migun/2/newbigfile.avi");
//        mtb.ResetMilli();
//        remove("/users/migun/2/newbigfile.avi");
//    }
    
//    FileCopyOperationJobNew job;
//    job.test2("/users/migun/ABRA/", VFSNativeHost::SharedHost());
    
//    job.test3("/Users/migun/", /*"Applications"*/ "!!", VFSNativeHost::SharedHost());
//    
//    auto host = VFSNativeHost::SharedHost();
//    vector<VFSFlexibleListingItem> items;
////    int ret = host->FetchFlexibleListingItems("/Users/migun/Downloads", {"gimp-2.8.14.dmg", "PopcornTime-latest.dmg", "TorBrowser-5.0.1-osx64_en-US.dmg"}, 0, items, nullptr);
////    int ret = host->FetchFlexibleListingItems("/Users/migun", {"Source"}, 0, items, nullptr);
//    int ret = host->FetchFlexibleListingItems("/Users/migun/Documents/Files/source/Files/3rd_party/built/include", {"boost"}, 0, items, nullptr);
//    
//
//    job.Init(move(items), "/Users/migun/!TEST", host, {});
//    job.Do_Hack();
//    
//    
//    
//    int a = 10;
//    return 0;
//}();

FileCopyOperationJobNew::PathCompositionType FileCopyOperationJobNew::AnalyzeInitialDestination(string &_result_destination, bool &_need_to_build) const
{
    VFSStat st;
    if( m_DestinationHost->Stat(m_InitialDestinationPath.c_str(), st, 0, nullptr ) == 0) {
        // destination entry already exist
        if( S_ISDIR(st.mode) ) {
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
FileCopyOperationJobNew::StepResult FileCopyOperationJobNew::BuildDestinationDirectory() const
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
                case OperationDialogResult::Stop:   return StepResult::Stop;
                default:                            continue;
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

tuple<FileCopyOperationJobNew::StepResult, FileCopyOperationJobNew::SourceItems> FileCopyOperationJobNew::ScanSourceItems() const
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
//            cout << _full_relative_path << " | " << _item_name << endl;
            
            // compose a full path for current entry
            string path = db.BaseDir(base_dir_indx) + _full_relative_path;
            
            // gather stat() information regarding current entry
            int ret;
            VFSStat st;
            while( (ret = host.Stat(path.c_str(), st, stat_flags, nullptr)) < 0 ) {
                if( m_SkipAll ) return StepResult::Skipped;
                switch( m_OnCantAccessSourceItem(ret, path) ) {
                    case OperationDialogResult::Skip:       return StepResult::Skipped;
                    case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
                    case OperationDialogResult::Stop:       return StepResult::Stop;
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
                            case OperationDialogResult::Skip:       return StepResult::Skipped;
                            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
                            case OperationDialogResult::Stop:       return StepResult::Stop;
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
FileCopyOperationJobNew::StepResult FileCopyOperationJobNew::CopyNativeFileToNativeFile(const string& _src_path,
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
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
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
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
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
        
        if( m_SkipAll )
            return StepResult::Skipped;
        
        auto setup_overwrite = [&]{
            dst_open_flags = O_WRONLY;
            do_unlink_on_stop = true;
            dst_size_on_stop = 0;
            do_erase_xattrs = true;
            preallocate_delta = src_stat_buffer.st_size - dst_stat_buffer.st_size; // negative value is ok here
            need_dst_truncate = src_stat_buffer.st_size < dst_stat_buffer.st_size;
        };
        auto setup_append = [&]{
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
        
        if( m_OverwriteAll )
            setup_overwrite();
        else if( m_AppendAll )
            setup_append();
        else switch( m_OnFileAlreadyExist( src_stat_buffer, dst_stat_buffer, _dst_path) ) {
                case FileCopyOperationDR::Overwrite:    setup_overwrite(); break;
                case FileCopyOperationDR::Append:       setup_append(); break;
                case OperationDialogResult::Skip:       return StepResult::Skipped;
                default:                                return StepResult::Stop;
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
            case OperationDialogResult::Retry:      continue;
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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
                case OperationDialogResult::Retry:      continue;
                case OperationDialogResult::Skip:       return StepResult::Skipped;
                case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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
                case OperationDialogResult::Retry:      continue;
                case OperationDialogResult::Skip:       return StepResult::Skipped;
                case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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
                        case OperationDialogResult::Retry:      continue;
                        case OperationDialogResult::Skip:       write_return = StepResult::Skipped; return;
                        case OperationDialogResult::SkipAll:    write_return = StepResult::SkipAll; return;
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
                    case OperationDialogResult::Retry:      continue;
                    case OperationDialogResult::Skip:       read_return = StepResult::Skipped; break;
                    case OperationDialogResult::SkipAll:    read_return = StepResult::SkipAll; break;
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
FileCopyOperationJobNew::StepResult FileCopyOperationJobNew::CopyVFSFileToNativeFile(VFSHost &_src_vfs,
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
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
        }
    }

    // create source file object
    VFSFilePtr src_file;
    while( (ret = _src_vfs.CreateFile(_src_path.c_str(), src_file)) < 0 ) {
        // failed to create file object
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
        }
    }

    // open source file
    while( (ret = src_file->Open(VFSFlags::OF_Read | VFSFlags::OF_ShLock | VFSFlags::OF_NoCache)) < 0 ) {
        // failed to open source file
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
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
        
        if( m_SkipAll )
            return StepResult::Skipped;
        
        auto setup_overwrite = [&]{
            dst_open_flags = O_WRONLY;
            do_unlink_on_stop = true;
            dst_size_on_stop = 0;
            do_erase_xattrs = true;
            preallocate_delta = src_stat_buffer.size - dst_stat_buffer.st_size; // negative value is ok here
            need_dst_truncate = src_stat_buffer.size < dst_stat_buffer.st_size;
        };
        auto setup_append = [&]{
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
        
        if( m_OverwriteAll )
            setup_overwrite();
        else if( m_AppendAll )
            setup_append();
        else switch( m_OnFileAlreadyExist( src_stat_buffer.SysStat(), dst_stat_buffer, _dst_path) ) {
            case FileCopyOperationDR::Overwrite:    setup_overwrite(); break;
            case FileCopyOperationDR::Append:       setup_append(); break;
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            default:                                return StepResult::Stop;
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
            case OperationDialogResult::Retry:      continue;
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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
                case OperationDialogResult::Retry:      continue;
                case OperationDialogResult::Skip:       return StepResult::Skipped;
                case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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
                case OperationDialogResult::Retry:      continue;
                case OperationDialogResult::Skip:       return StepResult::Skipped;
                case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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
                        case OperationDialogResult::Retry:      continue;
                        case OperationDialogResult::Skip:       write_return = StepResult::Skipped; return;
                        case OperationDialogResult::SkipAll:    write_return = StepResult::SkipAll; return;
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
            int64_t read_result =  src_file->Read(read_buffer + has_read, src_preffered_io_size);
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
                    case OperationDialogResult::Retry:      continue;
                    case OperationDialogResult::Skip:       read_return = StepResult::Skipped; break;
                    case OperationDialogResult::SkipAll:    read_return = StepResult::SkipAll; break;
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
        
        // swap buffers ang go again
        bytes_to_write = has_read;
        swap( read_buffer, write_buffer );
    }
    
    // we're ok, turn off destination cleaning
    clean_destination.disengage();
    
    
    // TODO: all attrs
    


    return StepResult::Ok;
}


FileCopyOperationJobNew::StepResult FileCopyOperationJobNew::CopyVFSFileToVFSFile(VFSHost &_src_vfs,
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
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
        }
    }
    
    // create source file object
    VFSFilePtr src_file;
    while( (ret = _src_vfs.CreateFile(_src_path.c_str(), src_file)) < 0 ) {
        // failed to create file object
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
        }
    }
    
    // open source file
    while( (ret = src_file->Open(VFSFlags::OF_Read | VFSFlags::OF_ShLock | VFSFlags::OF_NoCache)) < 0 ) {
        // failed to open source file
        if( m_SkipAll ) return StepResult::Skipped;
        switch( m_OnCantAccessSourceItem( ret, _src_path ) ) {
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            case OperationDialogResult::Stop:       return StepResult::Stop;
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
    initial_writing_offset  = 0;
    
    // stat destination
    VFSStat dst_stat_buffer;
//    if( io.stat(_dst_path.c_str(), &dst_stat_buffer) != -1 ) {
    if( m_DestinationHost->Stat(_dst_path.c_str(), dst_stat_buffer, 0, 0) == 0) {
        // file already exist. what should we do now?
        dst_existed_before = true;
        
        if( m_SkipAll )
            return StepResult::Skipped;
        
        auto setup_overwrite = [&]{
            dst_open_flags = VFSFlags::OF_Write | VFSFlags::OF_Truncate | VFSFlags::OF_NoCache;
            do_unlink_on_stop = true;
            dst_size_on_stop = 0;
            do_erase_xattrs = true;
            need_dst_truncate = src_stat_buffer.size < dst_stat_buffer.size;
        };
        auto setup_append = [&]{
            dst_open_flags = VFSFlags::OF_Write | VFSFlags::OF_Append | VFSFlags::OF_NoCache;
            do_unlink_on_stop = false;
            do_copy_xattrs = false;
            do_set_times = false;
            do_set_unix_flags = false;
            dst_size_on_stop = dst_stat_buffer.size;
            total_dst_size += dst_stat_buffer.size;
            initial_writing_offset = dst_stat_buffer.size;
        };
        
        if( m_OverwriteAll )
            setup_overwrite();
        else if( m_AppendAll )
            setup_append();
        else switch( m_OnFileAlreadyExist( src_stat_buffer.SysStat(), dst_stat_buffer.SysStat(), _dst_path) ) {
            case FileCopyOperationDR::Overwrite:    setup_overwrite(); break;
            case FileCopyOperationDR::Append:       setup_append(); break;
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            default:                                return StepResult::Stop;
        }
    }
    else {
        // no dest file - just create it
        dst_open_flags = VFSFlags::OF_Write | VFSFlags::OF_Create | VFSFlags::OF_NoCache;
        do_unlink_on_stop = true;
        dst_size_on_stop = 0;
    }
    
//    ret = m_DstHost->Stat(_dest_full_path.c_str(), dst_stat, 0, 0);
//    if(ret == 0) { //file already exist - what should we do?
//        int result;
//        if(m_SkipAll) goto cleanup;
//        if(m_OverwriteAll) goto dec_overwrite;
//        if(m_AppendAll) goto dec_append;
//        
//        result = [[m_Operation OnFileExist:_dest_full_path.c_str()
//                                   newsize:src_stat.size
//                                   newtime:src_stat.mtime.tv_sec
//                                   exisize:dst_stat.size
//                                   exitime:dst_stat.mtime.tv_sec
//                                  remember:&remember_choice] WaitForResult];
//        if(result == FileCopyOperationDR::Overwrite){ if(remember_choice) m_OverwriteAll = true;  goto dec_overwrite; }
//        if(result == FileCopyOperationDR::Append)   { if(remember_choice) m_AppendAll = true;     goto dec_append;    }
//        if(result == OperationDialogResult::Skip)   { if(remember_choice) m_SkipAll = true;       goto cleanup;      }
//        if(result == OperationDialogResult::Stop)   { RequestStop(); goto cleanup; }
//        
//        // decisions about what to do with existing destination
//    dec_overwrite:
//        dstopenflags = VFSFlags::OF_Write | VFSFlags::OF_Truncate | VFSFlags::OF_NoCache;
//        unlink_on_stop = true;
//        goto dec_end;
//    dec_append:
//        dstopenflags = VFSFlags::OF_Write | VFSFlags::OF_Append | VFSFlags::OF_NoCache;
//        totaldestsize += dst_stat.size;
//        startwriteoff = dst_stat.size;
//        unlink_on_stop = false;
//    dec_end:;
//    } else {
//        // no dest file - just create it
//        dstopenflags = VFSFlags::OF_Write | VFSFlags::OF_Create | VFSFlags::OF_NoCache;
//        unlink_on_stop = true;
//    }
    
    
    // open file object for destination
    VFSFilePtr dst_file;
    while( (ret = m_DestinationHost->CreateFile(_dst_path.c_str(), dst_file)) != 0 ) {
        // failed to create destination file
        if( m_SkipAll )
            return StepResult::Skipped;
        
        switch( m_OnCantOpenDestinationFile(ret, _dst_path) ) {
            case OperationDialogResult::Retry:      continue;
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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
            case OperationDialogResult::Retry:      continue;
            case OperationDialogResult::Skip:       return StepResult::Skipped;
            case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
            default:                                return StepResult::Stop;
        }
    }
    

//    
//    // for some circumstances we have to clean up remains if anything goes wrong
//    // and do it BEFORE close_destination fires
//    auto clean_destination = at_scope_end([&]{
//        if( destination_fd != -1 ) {
//            // we need to revert what we've done
//            ftruncate(destination_fd, dst_size_on_stop);
//            close(destination_fd);
//            destination_fd = -1;
//            if( do_unlink_on_stop )
//                io.unlink( _dst_path.c_str() );
//        }
//    });

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
                        case OperationDialogResult::Retry:      continue;
                        case OperationDialogResult::Skip:       write_return = StepResult::Skipped; return;
                        case OperationDialogResult::SkipAll:    write_return = StepResult::SkipAll; return;
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
            int64_t read_result =  src_file->Read(read_buffer + has_read, src_preffered_io_size);
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
                    case OperationDialogResult::Retry:      continue;
                    case OperationDialogResult::Skip:       read_return = StepResult::Skipped; break;
                    case OperationDialogResult::SkipAll:    read_return = StepResult::SkipAll; break;
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
        
        // swap buffers ang go again
        bytes_to_write = has_read;
        swap( read_buffer, write_buffer );
    }
    
    
//    while( total_wrote < src_stat.size )
//    {
//        if(CheckPauseOrStop()) goto cleanup;
//        
//    doread: ssize_t read_amount = src_file->Read(m_Buffer.get(), m_BufferSize);
//        if(read_amount < 0)
//        {
//            if(m_SkipAll) goto cleanup;
//            int result = [[m_Operation OnCopyReadError:VFSError::ToNSError((int)read_amount) ForFile:_dest_full_path.c_str()] WaitForResult];
//            if(result == OperationDialogResult::Retry) goto doread;
//            if(result == OperationDialogResult::Skip) goto cleanup;
//            if(result == OperationDialogResult::SkipAll) { m_SkipAll = true; goto cleanup; }
//            if(result == OperationDialogResult::Stop) { RequestStop(); goto cleanup; }
//        }
//        
//        size_t to_write = read_amount;
//        while(to_write > 0)
//        {
//        dowrite: ssize_t write_amount = dst_file->Write(m_Buffer.get(), to_write);
//            if(write_amount < 0)
//            {
//                if(m_SkipAll) goto cleanup;
//                int result = [[m_Operation OnCopyWriteError:VFSError::ToNSError((int)write_amount) ForFile:_dest_full_path.c_str()] WaitForResult];
//                if(result == OperationDialogResult::Retry) goto dowrite;
//                if(result == OperationDialogResult::Skip) goto cleanup;
//                if(result == OperationDialogResult::SkipAll) { m_SkipAll = true; goto cleanup; }
//                if(result == OperationDialogResult::Stop) { RequestStop(); goto cleanup; }
//            }
//            
//            to_write -= write_amount;
//            total_wrote += write_amount;
//            m_TotalCopied += write_amount;
//        }
//        
//        // update statistics
//        m_Stats.SetValue(m_TotalCopied);
//    }
    
    
    // we're ok, turn off destination cleaning
//    clean_destination.disengage();
    
    
    // TODO: all attrs
    
    
    
    
    return StepResult::Ok;
}

// uses m_Buffer[0] to reduce mallocs
// currently there's no error handling or reporting here. may need this in the future. maybe.
void FileCopyOperationJobNew::EraseXattrsFromNativeFD(int _fd_in) const
{
    auto xnames = (char*)m_Buffers[0].get();
    auto xnamesizes = flistxattr(_fd_in, xnames, m_BufferSize, 0);
    for( auto s = xnames, e = xnames + xnamesizes; s < e; s += strlen(s) + 1 ) // iterate thru xattr names..
        fremovexattr(_fd_in, s, 0); // ..and remove everyone
}

// uses m_Buffer[0] and m_Buffer[1] to reduce mallocs
// currently there's no error handling or reporting here. may need this in the future. maybe.
void FileCopyOperationJobNew::CopyXattrsFromNativeFDToNativeFD(int _fd_from, int _fd_to) const
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

FileCopyOperationJobNew::StepResult FileCopyOperationJobNew::CopyNativeDirectoryToNativeDirectory(const string& _src_path,
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
                case OperationDialogResult::Retry:      continue;
                case OperationDialogResult::Skip:       return StepResult::Skipped;
                case OperationDialogResult::SkipAll:    return StepResult::SkipAll;
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

FileCopyOperationJobNew::StepResult FileCopyOperationJobNew::RenameNativeFile(const string& _src_path,
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

            switch( m_OnRenameDestinationAlreadyExists( _src_path, _dst_path ) ) {
                case FileCopyOperationDR::Skip:       	return StepResult::Skipped;
                case FileCopyOperationDR::SkipAll:      return StepResult::SkipAll;
                case FileCopyOperationDR::Stop:         return StepResult::Stop;
                case FileCopyOperationDR::Overwrite:    break;
                default:                                return StepResult::Stop;
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

void FileCopyOperationJobNew::CleanSourceItems() const
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

FileCopyOperationJobNew::StepResult FileCopyOperationJobNew::VerifyCopiedFile(const ChecksumExpectation& _exp, bool &_matched) const
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
    
    // show a dialog?
    
    return StepResult::Ok;
}
