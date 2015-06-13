//
//  BatchRenameOperation.m
//  Files
//
//  Created by Michael G. Kazakov on 11/06/15.
//  Copyright (c) 2015 Michael G. Kazakov. All rights reserved.
//

#import "BatchRenameOperation.h"
#import "BatchRenameOperationJob.h"

@implementation BatchRenameOperation
{
    BatchRenameOperationJob m_Job;
}

- (id)initWithOriginalFilepaths:(vector<string>&&)_src_paths
               renamedFilepaths:(vector<string>&&)_dst_paths
                            vfs:(VFSHostPtr)_src_vfs
{
    self = [super initWithJob:&m_Job];
    if (self) {
        m_Job.Init(move(_src_paths), move(_dst_paths), _src_vfs, self);
    }
    return self;
}


@end
