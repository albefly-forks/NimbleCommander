#include <string>
#include <Utility/FontExtras.h>

using namespace std;

@implementation NSFont (StringDescription)

+ (NSFont*) fontWithStringDescription:(NSString*)_description
{
    if( !_description )
        return nil;
    
    NSArray *arr = [_description componentsSeparatedByString:@","];
    if( !arr || arr.count != 2 )
        return nil;
    
    NSString *family = arr[0];
    NSString *size = [arr[1] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    const auto sz = size.intValue;
    if( family.length == 0 || [family characterAtIndex:0] != '@' ) {
        // regular "family,size" syntax
        return [NSFont fontWithName:family size:sz];
    }
    else {
        if( [family isEqualToString:@"@systemFont"] )
            return [NSFont systemFontOfSize:sz];
        if( [family isEqualToString:@"@boldSystemFont"] )
            return [NSFont boldSystemFontOfSize:sz];
        if( [family isEqualToString:@"@labelFont"] )
            return [NSFont labelFontOfSize:sz];
        if( [family isEqualToString:@"@titleBarFont"] )
            return [NSFont titleBarFontOfSize:sz];
        if( [family isEqualToString:@"@menuFont"] )
            return [NSFont menuFontOfSize:sz];
        if( [family isEqualToString:@"@menuBarFont"] )
            return [NSFont menuBarFontOfSize:sz];
        if( [family isEqualToString:@"@messageFont"] )
            return [NSFont messageFontOfSize:sz];
        if( [family isEqualToString:@"@paletteFont"] )
            return [NSFont paletteFontOfSize:sz];
        if( [family isEqualToString:@"@toolTipsFont"] )
            return [NSFont toolTipsFontOfSize:sz];
        if( [family isEqualToString:@"@controlContentFont"] )
            return [NSFont controlContentFontOfSize:sz];
        return nil;
    }
}

- (NSString*) toStringDescription
{
    return [NSString stringWithFormat:@"%@, %s",
            self.fontName,
            to_string(int(floor(self.pointSize + 0.5))).c_str()
            ];
}

@end

FontGeometryInfo::FontGeometryInfo(NSFont *_font):
    FontGeometryInfo( (__bridge CTFontRef)_font )
{
}

vector<short> FontGeometryInfo::CalculateStringsWidths( const vector<CFStringRef> &_strings, NSFont *_font )
{
    static const auto path = CGPathCreateWithRect(CGRectMake(0, 0, CGFLOAT_MAX, CGFLOAT_MAX), NULL);
    static const auto items_per_chunk = 300;
    
    auto attrs = @{NSFontAttributeName:_font};
    
    const auto count = (int)_strings.size();
    vector<short> widths( count );
    
    vector<NSRange> chunks;
    for( int i = 0; i < count; i += items_per_chunk )
        chunks.emplace_back( NSMakeRange(i, min(items_per_chunk, count - i)) );
    
    dispatch_group_t group = dispatch_group_create();
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
    for( auto r: chunks )
        dispatch_group_async(group, queue, [&, r]{
            CFMutableStringRef storage = CFStringCreateMutable(NULL, r.length * 100);
            for( auto i = (int)r.location; i < r.location + r.length; ++i ) {
                CFStringAppend(storage, _strings[i]);
                CFStringAppend(storage, CFSTR("\n"));
            }
            
            const auto storage_length = CFStringGetLength(storage);
            CFAttributedStringRef stringRef = CFAttributedStringCreate(NULL, storage, (CFDictionaryRef)attrs);
            CTFramesetterRef framesetterRef = CTFramesetterCreateWithAttributedString(stringRef);
            CTFrameRef frameRef = CTFramesetterCreateFrame(framesetterRef, CFRangeMake(0, storage_length), path, NULL);
            NSArray *lines = (__bridge NSArray*)CTFrameGetLines(frameRef);
            int i = 0;
            for( id item in lines ) {
                CTLineRef line = (__bridge CTLineRef)item;
                double lineWidth = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
                widths[ r.location + i++ ] = (short)floor( lineWidth + 0.5 );
            }
            CFRelease(frameRef);
            CFRelease(framesetterRef);
            CFRelease(stringRef);
            CFRelease(storage);
        });
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    return widths;
}
