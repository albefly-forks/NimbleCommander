// Copyright (C) 2020 Michael Kazakov. Subject to GNU General Public License version 3.

#include "SparkleShim.h"
#include <Sparkle/Sparkle.h>

SUUpdater *NCBootstrapSharedSUUpdaterInstance()
{
#ifdef __NC_VERSION_TRIAL__
    return [SUUpdater sharedUpdater];
#else
    return nil;
#endif
}

SEL NCBootstrapSUUpdaterAction()
{
    return @selector(checkForUpdates:);
}
