#pragma once

#include "../Operation.h"
#include <VFS/VFS.h>



/*
+TODO:
- dialogs
- adjusting stats on skips

*/


namespace nc::ops
{

class CompressionJob;

class Compression : public Operation
{
public:
    Compression(vector<VFSListingItem> _src_files,
                string _dst_root,
                VFSHostPtr _dst_vfs);
    virtual ~Compression();

    string ArchivePath() const;

protected:
    virtual Job *GetJob() noexcept override;

private:
    unique_ptr<CompressionJob> m_Job;
};

}