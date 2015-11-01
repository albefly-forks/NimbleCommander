//
//  CFString.h
//  Habanero
//
//  Created by Michael G. Kazakov on 03/09/15.
//  Copyright (c) 2015 MIchael Kazakov. All rights reserved.
//

#pragma once

#include <CoreFoundation/CoreFoundation.h>
#ifdef __OBJC__
    #import <Foundation/Foundation.h>
#endif
#include <string>

class CFString
{
public:
    CFString();
    CFString(const std::string &_str);
    CFString(const std::string &_str, CFStringEncoding _encoding);
    CFString(const char *_str);
    CFString(const CFString &_rhs);
    CFString(CFString &&_rhs);
    ~CFString();
    
    const CFString &operator=(const CFString &_rhs) noexcept;
    const CFString &operator=(CFString &&_rhs) noexcept;
    
    inline              operator bool() const noexcept { return p != nullptr; }
    inline CFStringRef  operator    *() const noexcept { return p; }
#ifdef __OBJC__
    inline NSString               *ns() const noexcept { return (__bridge NSString*)p; }
#endif

private:
    CFStringRef p;
};