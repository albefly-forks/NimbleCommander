#pragma once

#include <Cocoa/Cocoa.h>

namespace nc::ops {
    class Pool;

}

// STA design - use it only from main queue
@interface NCOpsPoolViewController : NSViewController

- (instancetype) initWithPool:(nc::ops::Pool&)_pool;

@end