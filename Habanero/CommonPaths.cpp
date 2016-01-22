//
//  common_paths.mm
//  Files
//
//  Created by Michael G. Kazakov on 24.12.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import <CoreFoundation/CoreFoundation.h>
#import <sys/param.h>
#import <pwd.h>
#import <unistd.h>
#import "CommonPaths.h"

namespace CommonPaths
{

static std::string GetMainBundlePath()
{
    CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    char path[MAXPATHLEN];
    bool result = CFURLGetFileSystemRepresentation(url, true, (UInt8*)path, MAXPATHLEN);
    CFRelease(url);
    return result ? std::string(path) : std::string("");
}

static std::string ensure_tr_slash( std::string _str )
{
    if(_str.empty() || _str.back() != '/')
        _str += '/';
    return _str;
}

const std::string &AppBundle() noexcept
{
    static auto path = ensure_tr_slash(GetMainBundlePath());
    return path;
}

const std::string &Home() noexcept
{
    static auto path = ensure_tr_slash(getpwuid(getuid())->pw_dir);
    return path;
}

const std::string &Documents() noexcept
{
    static auto path = Home() + "Documents/";
    return path;
}

const std::string &Desktop() noexcept
{
    static auto path = Home() + "Desktop/";
    return path;
}

const std::string &Downloads() noexcept
{
    static auto path = Home() + "Downloads/";
    return path;
}

const std::string &Applications() noexcept
{
    static auto path = std::string("/Applications/");
    return path;
}

const std::string &Utilities() noexcept
{
    static auto path = std::string("/Applications/Utilities/");
    return path;
}

const std::string &Library() noexcept
{
    static auto path = Home() + "Library/";
    return path;
}

const std::string &Pictures() noexcept
{
    static auto path = Home() + "Pictures/";
    return path;
}

const std::string &Music() noexcept
{
    static auto path = Home() + "Music/";
    return path;
}

const std::string &Movies() noexcept
{
    static auto path = Home() + "Movies/";
    return path;
}

const std::string &Root() noexcept
{
    static auto path = std::string("/");
    return path;
}
    
}
