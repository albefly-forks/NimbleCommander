//
//  VFSFile.mm
//  Files
//
//  Created by Michael G. Kazakov on 26.08.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import "VFSFile.h"
#import "VFSError.h"
#import "VFSHost.h"

VFSFile::VFSFile(const char* _relative_path, std::shared_ptr<VFSHost> _host):
    m_RelativePath(_relative_path),
    m_Host(_host)
{
}

VFSFile::~VFSFile()
{
}

const char* VFSFile::RelativePath() const
{
    return m_RelativePath.c_str();
}

std::shared_ptr<VFSHost> VFSFile::Host() const
{
    return m_Host;
}

VFSFile::ReadParadigm VFSFile::GetReadParadigm() const
{
    return ReadParadigm::NoRead;
}

ssize_t VFSFile::Read(void *_buf, size_t _size)
{
    return VFSError::NotSupported;
}

ssize_t VFSFile::ReadAt(off_t _pos, void *_buf, size_t _size)
{
    return VFSError::NotSupported;
}

bool VFSFile::IsOpened() const
{
    return false;
}

int     VFSFile::Open(int)          { return VFSError::NotSupported; }
int     VFSFile::Close()            { return VFSError::NotSupported; }
off_t   VFSFile::Seek(off_t, int)   { return VFSError::NotSupported; }
ssize_t VFSFile::Pos() const        { return VFSError::NotSupported; }
ssize_t VFSFile::Size() const       { return VFSError::NotSupported; }
bool    VFSFile::Eof() const        { return true; }
std::shared_ptr<VFSFile> VFSFile::Clone() const { return 0; }

void VFSFile::ComposeFullHostsPath(char *_buf) const
{
    // this can be more complex for network vfs - maybe make this function virtual in the future
    // can be optimized
    if(m_RelativePath.empty() && !m_Host.get())
    {
        strcpy(_buf, "");
        return;
    }
    
    VFSHost *hosts[32];
    int hosts_n = 0;

    VFSHost *cur = m_Host.get();
    while(cur->Parent().get() != 0) // skip the root host
    {
        hosts[hosts_n++] = cur;
        cur = cur->Parent().get();
    }
    
    strcpy(_buf, "");
    while(hosts_n > 0)
        strcat(_buf, hosts[--hosts_n]->JunctionPath());
//    if(_buf[strlen(_buf)-1]!='/') strcat(_buf, "/");
    assert(m_RelativePath.c_str()[0] == '/');
    strcat(_buf, m_RelativePath.c_str());
}