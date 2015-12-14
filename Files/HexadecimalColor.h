#pragma once

uint32_t HexadecimalColorStringToRGBA( string_view _string ) noexcept;
void HexadecimalColorRGBAToString( uint32_t _rgba, char _string[10] ) noexcept;

#ifdef __OBJC__

@interface NSColor (HexColorInterface)

+ (NSColor*)colorWithRGBA:(uint32_t)_rgba;
+ (NSColor*)colorWithHexString:(const char*)_hex;
+ (NSColor*)colorWithHexStdString:(const string&)_hex;
- (uint32_t)toRGBA;
- (NSString*)toHexString;

@end

#endif