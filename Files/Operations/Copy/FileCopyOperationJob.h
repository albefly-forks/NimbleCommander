//
//  FileCopyOperationJob.h
//  Files
//
//  Created by Michael G. Kazakov on 30/01/15.
//  Copyright (c) 2015 Michael G. Kazakov. All rights reserved.
//

#import "OperationJob.h"

class FileCopyOperationJob  : public OperationJob
{
public:
    
    static bool ShouldPreallocateSpace(int64_t _bytes_to_write, int _file_des);

    // PreallocateSpace assumes following ftruncate, meaningless otherwise
    static void PreallocateSpace(int64_t _preallocate_delta, int _file_des);
    
protected:
    vector<string>  m_InitialItems;
    string          m_InitialSource;
    string          m_InitialDestination;
};