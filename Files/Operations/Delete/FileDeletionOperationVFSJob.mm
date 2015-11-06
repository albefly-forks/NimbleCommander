//
//  FileDeletionOperationVFSJob.cpp
//  Files
//
//  Created by Michael G. Kazakov on 08.05.14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#include "FileDeletionOperationVFSJob.h"
#include "../../OperationDialogAlert.h"

void FileDeletionOperationVFSJob::Init(vector<string>&& _files,
                                       const string &_root,
                                       const VFSHostPtr& _host,
                                       FileDeletionOperation *_op)
{
    assert(_host);
    assert(!_host->IsNativeFS()); // for native FS please use direct FileDeletionOperationJob
    assert(_op);
    m_RequestedFiles = move(_files);
    m_RootPath = _root;
    m_Host = _host;
    m_Operation = _op;
    
    assert(m_RootPath.is_absolute());
}

void FileDeletionOperationVFSJob::Do()
{
    DoScan();
    
    m_Stats.StartTimeTracking();
    m_Stats.SetMaxValue(m_ItemsToDelete.size());
        
    for(auto &i: m_ItemsToDelete)
    {
        if(CheckPauseOrStop()) { SetStopped(); return; }
        m_Stats.SetCurrentItem(i.c_str());
        string name = i.to_str_with_pref();
        path path = m_RootPath / name;
        if(name.back() == '/')
        { // dir
            retry_rmdir:;
            int ret = m_Host->RemoveDirectory(path.c_str(), 0);
            if(ret != 0 && !m_SkipAll)
            {
                int result = [[m_Operation DialogOnRmdirError:VFSError::ToNSError(ret) ForPath:path.c_str()] WaitForResult];
                if (result == OperationDialogResult::Retry) goto retry_unlink;
                else if (result == OperationDialogResult::SkipAll) m_SkipAll = true;
                else if (result == OperationDialogResult::Stop) RequestStop();
            }
        }
        else
        { // reg or lnk
            retry_unlink:;
            int ret = m_Host->Unlink(path.c_str(), 0);
            if(ret != 0 && !m_SkipAll)
            {
                int result = [[m_Operation DialogOnUnlinkError:VFSError::ToNSError(ret) ForPath:path.c_str()] WaitForResult];
                if (result == OperationDialogResult::Retry) goto retry_unlink;
                else if (result == OperationDialogResult::SkipAll) m_SkipAll = true;
                else if (result == OperationDialogResult::Stop) RequestStop();
            }
        }
        
        m_Stats.AddValue(1);        
    }
    
    m_Stats.SetCurrentItem("");
    
    if(CheckPauseOrStop()) { SetStopped(); return; }
    SetCompleted();
}

void FileDeletionOperationVFSJob::DoScan()
{
    for(auto &i: m_RequestedFiles)
    {
        if (CheckPauseOrStop()) return;
    
        path fn = m_RootPath / i.c_str();
        VFSStat st;
        
        // currently silently ignores files which failed to stat. not sure if this is ok.
        if( m_Host->Stat(fn.c_str(), st, VFSFlags::F_NoFollow, 0) == 0 )
        {
            if( (st.mode & S_IFMT) == S_IFREG )
            {
                m_ItemsToDelete.push_back(i.c_str(), (unsigned)i.size(), nullptr);
            }
            else if( (st.mode & S_IFMT) == S_IFLNK )
            {
                m_ItemsToDelete.push_back(i.c_str(), (unsigned)i.size(), nullptr);
            }
            else if( (st.mode & S_IFMT) == S_IFDIR )
            {
                // add new dir in our tree structure
                m_Directories.push_back(string(i.c_str()) + '/', nullptr);
                auto dirnode = &m_Directories.back();

                // add all items in directory
                DoScanDir(fn, dirnode);
                
                // add directory itself at the end, since we need it to be deleted last of all
                m_ItemsToDelete.push_back(string(i.c_str()) + '/', nullptr);
            }
        }
    }
}

void FileDeletionOperationVFSJob::DoScanDir(const path &_full_path, const chained_strings::node *_prefix)
{
retry_iterate:;
    int ret = m_Host->IterateDirectoryListing(_full_path.c_str(), [&](const VFSDirEnt &_dirent)
    {
        if(_dirent.type == VFSDirEnt::Reg ||
           _dirent.type == VFSDirEnt::Link)
        {
            m_ItemsToDelete.push_back(_dirent.name, _dirent.name_len, _prefix);
        }
        else if(_dirent.type == VFSDirEnt::Dir)
        {
            m_Directories.push_back(string(_dirent.name) + '/', _prefix);
            auto dirnode = &m_Directories.back();
            DoScanDir(_full_path / _dirent.name, dirnode);
            m_ItemsToDelete.push_back(string(_dirent.name) + '/', _prefix);
        }
        return true;
    });
    if(ret != 0 && !m_SkipAll)
    {
        int result = [[m_Operation DialogOnOpendirError:VFSError::ToNSError(ret) ForDir:_full_path.c_str()] WaitForResult];
        if (result == OperationDialogResult::Retry) goto retry_iterate;
        else if (result == OperationDialogResult::SkipAll) m_SkipAll = true;
        else if (result == OperationDialogResult::Stop) RequestStop();
    }
}