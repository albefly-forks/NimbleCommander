//
//  VFSNativeListing.mm
//  Files
//
//  Created by Michael G. Kazakov on 26.08.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import "VFSNativeListing.h"
#import "VFSNativeHost.h"
#import <sys/types.h>
#import <sys/dirent.h>
#import <sys/stat.h>
#import <dirent.h>
#import <stddef.h>
#import <fcntl.h>
#import <string.h>
#import <memory.h>
#import <stdlib.h>
#import <assert.h>

// hack to access function from libc implementation directly.
// this func does readdir but without mutex locking
struct dirent	*_readdir_unlocked(DIR *, int) __DARWIN_INODE64(_readdir_unlocked);

VFSNativeListing::VFSNativeListing(const char *_path, std::shared_ptr<VFSNativeHost> _host):
    VFSListing(_path, _host)
{
}

VFSNativeListing::~VFSNativeListing()
{
    EraseListing();
}

int VFSNativeListing::LoadListingData(bool (^_checker)())
{
//    int aa = sizeof(VFSListingItem);
//    int a = sizeof(VFSNativeListingItem);
//    assert(sizeof(VFSNativeListingItem) == 128);
//    _target->clear();
    EraseListing();
    
    DIR *dirp = opendir(RelativePath());
    if(!dirp)
        return VFSError::FromErrno(errno);
    
    if(_checker && _checker())
    {
        closedir(dirp);
        return VFSError::Cancelled;
    }
    
    dirent *entp;
    
    bool need_to_add_dot_dot = true; // in some fancy situations there's no ".." entry in directory - we should insert it by hand
    
    char pathwithslash[MAXPATHLEN]; // this buffer will be used for composing long filenames for stat()
    char *pathwithslashp = &pathwithslash[0];
    strcpy(pathwithslash, RelativePath());
    if(pathwithslash[strlen(pathwithslash)-1] != '/' ) strcat(pathwithslash, "/");
    size_t pathwithslash_len = strlen(pathwithslash);
    
    while((entp = _readdir_unlocked(dirp, 1)) != NULL)
    {
        if(_checker && _checker())
        {
            closedir(dirp);
            return 0;
        }
        
        if(entp->d_ino == 0) continue; // apple's documentation suggest to skip such files
        if(entp->d_namlen == 1 && entp->d_name[0] ==  '.') continue; // do not process self entry
        if(entp->d_namlen == 2 && entp->d_name[0] ==  '.' && entp->d_name[1] ==  '.') // special case for dot-dot directory
        {
            need_to_add_dot_dot = false;
            
            if(strcmp(pathwithslash, "/") == 0)
                continue; // skip .. for root directory
            
            // TODO: handle situation when ".." is not the #0 entry
            
            // it's very nice that sometimes OSX can not set a valid flags on ".." file in a mount point
            // so for now - just fix it by hand
            if(entp->d_type == 0)
                entp->d_type = DT_DIR; // a very-very strange bugfix
        }
        
        m_Items.push_back(VFSNativeListingItem() = {}); // check me twise - does it really zeroing all members?
        
        VFSNativeListingItem &current = m_Items.back();
//        memset(&current, 0, sizeof(VFSNativeListingItem));
        current.unix_type = entp->d_type;
        current.inode  = entp->d_ino;
        current.namelen = entp->d_namlen;
        if(current.namelen < 14)
        {
            memcpy(&current.namebuf[0], &entp->d_name[0], current.namelen+1);
        }
        else
        {
            char *news = (char*)malloc(current.namelen+1);
            memcpy(news, &entp->d_name[0], current.namelen+1);
            *(char**)(&current.namebuf[0]) = news;
        }
    }
    
    closedir(dirp);
    
    if(need_to_add_dot_dot)
    {
        // ?? do we need to handle properly the usual ".." appearance, since we have a fix-up way anyhow?
        // add ".." entry by hand
        VFSNativeListingItem current = {};
//        memset(&current, 0, sizeof(DirectoryEntryInformation));
        current.unix_type = DT_DIR;
        current.inode  = 0;
        current.namelen = 2;
        memcpy(&current.namebuf[0], "..", current.namelen+1);
        current.size = VFSListingItem::InvalidSize;
        m_Items.insert(m_Items.begin(), current); // this can be looong on biiiiiig directories
    }
    
    // stat files, find extenstions any any and create CFString name representations in several threads
    dispatch_apply(m_Items.size(), dispatch_get_global_queue(0, 0), ^(size_t n) {
        VFSNativeListingItem *current = &m_Items[n];
        if(_checker && _checker()) return;
        char filename[__DARWIN_MAXPATHLEN];
        const char *entryname = current->Name();
        memcpy(filename, pathwithslashp, pathwithslash_len);
        memcpy(filename + pathwithslash_len, entryname, current->namelen+1);
        
        // stat the file
        struct stat stat_buffer;
        if(stat(filename, &stat_buffer) == 0)
        {
            current->atime = stat_buffer.st_atimespec.tv_sec;
            current->mtime = stat_buffer.st_mtimespec.tv_sec;
            current->ctime = stat_buffer.st_ctimespec.tv_sec;
            current->btime = stat_buffer.st_birthtimespec.tv_sec;
            current->unix_mode  = stat_buffer.st_mode;
            current->unix_flags = stat_buffer.st_flags;
            current->unix_uid   = stat_buffer.st_uid;
            current->unix_gid   = stat_buffer.st_gid;
            if( (stat_buffer.st_mode & S_IFMT) != S_IFDIR )
                current->size  = stat_buffer.st_size;
            else
                current->size = VFSListingItem::InvalidSize;
            // add other stat info here. there's a lot more
        }
        
        // parse extension if any
        for(int i = current->namelen - 1; i >= 0; --i)
            if(entryname[i] == '.')
            {
                if(i == current->namelen - 1 || i == 0)
                    break; // degenerate case, lets think that there's no extension at all
                current->extoffset = i+1; // CHECK THIS! may be some bugs with UTF
                break;
            }
        
        // create CFString name representation
        current->cf_name = CFStringCreateWithBytesNoCopy(0,
                                                         (UInt8*)entryname,
                                                         current->namelen,
                                                         kCFStringEncodingUTF8,
                                                         false,
                                                         kCFAllocatorNull);
        
        // if we're dealing with a symlink - read it's content to know the real file path
        if( current->unix_type == DT_LNK )
        {
            char linkpath[__DARWIN_MAXPATHLEN];
            ssize_t sz = readlink(filename, linkpath, __DARWIN_MAXPATHLEN);
            if(sz != -1)
            {
                linkpath[sz] = 0;
                char *s = (char*)malloc(sz+1);
                memcpy(s, linkpath, sz+1);
                current->symlink = s;
            }
        }
    });
    
    if(_checker && _checker())
        return VFSError::Cancelled;
    
//    if(_target->size() == 0)
//        return -1; // something was very wrong
    
    return VFSError::Ok;
}

void VFSNativeListing::EraseListing()
{
    for(auto &i :m_Items)
        i.Destroy();
    m_Items.clear();    
}

VFSListingItem& VFSNativeListing::At(int _position)
{
    assert(_position >= 0 && _position < m_Items.size());
    return m_Items[_position];
}

const VFSListingItem& VFSNativeListing::At(int _position) const
{
    assert(_position >= 0 && _position < m_Items.size());
    return m_Items[_position];
}

int VFSNativeListing::Count() const
{
    return (int)m_Items.size();
}
