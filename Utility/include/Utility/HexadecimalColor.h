#pragma once

#include <stdint.h>
#include <experimental/string_view>

using namespace std;
using namespace std::experimental;

uint32_t HexadecimalColorStringToRGBA( string_view _string ) noexcept;
void HexadecimalColorRGBAToString( uint32_t _rgba, char _string[10] ) noexcept;

#ifdef __OBJC__

#include <Cocoa/Cocoa.h>

@interface NSColor (HexColorInterface)

+ (NSColor*)colorWithRGBA:(uint32_t)_rgba;
+ (NSColor*)colorWithHexString:(const char*)_hex;
+ (NSColor*)colorWithHexStdString:(const string&)_hex;
- (uint32_t)toRGBA;
- (NSString*)toHexString;
- (string)toHexStdString;

@end

#endif