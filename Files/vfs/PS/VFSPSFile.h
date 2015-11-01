//
//  VFSPSFile.h
//  Files
//
//  Created by Michael G. Kazakov on 27.12.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#pragma once

#include "../VFSGenericMemReadOnlyFile.h"
#include "VFSPSHost.h"

class VFSPSFile : public VFSGenericMemReadOnlyFile
{
public:
    VFSPSFile(const char* _relative_path, shared_ptr<VFSHost> _host, const string &_file);
};