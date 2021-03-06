// Copyright (C) 2021 Michael Kazakov. Subject to GNU General Public License version 3.
#include "Trash.h"
#include <Foundation/Foundation.h>
#include <Habanero/CFPtr.h>

namespace nc::routedio {

int TrashItemAtPath(const char *_path) noexcept
{
    const auto url = nc::base::CFPtr<CFURLRef>::adopt(
        CFURLCreateFromFileSystemRepresentation(0, (const UInt8 *)_path, strlen(_path), false));
    
    if( !url ) {
        errno = ENOENT;
        return -1;
    }
    
    NSError *error;
    const auto result = [NSFileManager.defaultManager trashItemAtURL:(__bridge NSURL *)url.get()
                                                    resultingItemURL:nil
                                                               error:&error];

    if( result ) {
        return 0;
    }
    else {
        if( error != nil && [error.domain isEqualToString:NSPOSIXErrorDomain] ) {
            errno = static_cast<int>(error.code);
        }
        else {
            errno = EPERM;
        }
        return -1;
    }
}

} // namespace nc::routedio
