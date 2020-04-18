// Copyright (C) 2020 Michael Kazakov. Subject to GNU General Public License version 3.
#include "InterpreterImpl.h"
#include <Habanero/CFString.h>
#include <Habanero/CFPtr.h>
#include <Utility/OrthodoxMonospace.h>

namespace nc::term {

static std::u32string ConvertUTF8ToUTF32( std::string_view _utf8 );
static std::u32string ComposeUnicodePoints( std::u32string _utf32 );

InterpreterImpl::InterpreterImpl(Screen &_screen):
    m_Screen(_screen)
{
    m_Extent.height = m_Screen.Height();
    m_Extent.width = m_Screen.Width();
    m_Extent.top = 0;
    m_Extent.bottom = m_Screen.Height();
    ResetToDefaultTabStops(m_TabStops);
}

InterpreterImpl::~InterpreterImpl()
{
}

void InterpreterImpl::Interpret( Input _to_interpret )
{
    for( const auto &command: _to_interpret ) {
        using namespace input;
        const auto type = command.type;
        switch (type) {
            case Type::text:
                ProcessText( *std::get_if<UTF8Text>(&command.payload) );
                break;
            case Type::line_feed:
                ProcessLF();
                break;
            case Type::carriage_return:
                ProcessCR();
                break;
            case Type::back_space:
                ProcessBS();
                break;
            case Type::reverse_index:
                ProcessRI();
                break;
            case Type::move_cursor:
                ProcessMC( *std::get_if<CursorMovement>(&command.payload) );
                break;
            case Type::horizontal_tab:
                ProcessHT( *std::get_if<signed>(&command.payload) );
                break;
            case Type::report:
                ProcessReport( *std::get_if<DeviceReport>(&command.payload) );
                break;
            case Type::bell:
                ProcessBell();
                break;
            default:
                break;
        }
    }
}

void InterpreterImpl::SetOuput( Output _output )
{
    m_Output = std::move(_output);
}

void InterpreterImpl::SetBell( Bell _bell )
{
    m_Bell = std::move(_bell);
}

void InterpreterImpl::ProcessText( const input::UTF8Text &_text )
{
    const auto utf32 = ComposeUnicodePoints( ConvertUTF8ToUTF32( _text.characters ) );
    
    for( const auto c: utf32 ) {
    
//    
//    // TODO: if(wrapping_mode == ...) <- need to add this
        if( m_Screen.CursorX() >= m_Screen.Width() && !oms::IsUnicodeCombiningCharacter(c) ) {
            m_Screen.PutWrap();
            ProcessCR();
            ProcessLF();
        }
//
//        if(m_InsertMode)
//            m_Scr.DoShiftRowRight(oms::WCWidthMin1(c));    
//    
        m_Screen.PutCh(c);
    }
    // TODO: MUCH STUFF
}

void InterpreterImpl::ProcessLF()
{
    if( m_Screen.CursorY() + 1 == m_Extent.bottom )
        m_Screen.DoScrollUp( m_Extent.top, m_Extent.bottom, 1 );
    else
        m_Screen.DoCursorDown();
}

void InterpreterImpl::ProcessCR()
{
    m_Screen.GoTo( 0, m_Screen.CursorY() );
}

void InterpreterImpl::ProcessBS()
{
    m_Screen.DoCursorLeft();
}

void InterpreterImpl::ProcessRI()
{
    if( m_Screen.CursorY() == m_Extent.top )
        m_Screen.ScrollDown( m_Extent.top, m_Extent.bottom, 1);
    else
        m_Screen.DoCursorUp();
}

void InterpreterImpl::ProcessMC( const input::CursorMovement _cursor_movement )
{
    if( _cursor_movement.positioning == input::CursorMovement::Absolute ) {
        if( _cursor_movement.x != std::nullopt && _cursor_movement.y != std::nullopt ) {
            m_Screen.GoTo( *_cursor_movement.x, *_cursor_movement.y );
        }
        else if( _cursor_movement.x != std::nullopt && _cursor_movement.y == std::nullopt ) {
            m_Screen.GoTo( *_cursor_movement.x, m_Screen.CursorY() );
        }
        else if( _cursor_movement.x == std::nullopt && _cursor_movement.y != std::nullopt ) {
            m_Screen.GoTo( m_Screen.CursorX(), *_cursor_movement.y );
        }
    }
    if( _cursor_movement.positioning == input::CursorMovement::Relative ) {
        const int x = m_Screen.CursorX();
        const int y = m_Screen.CursorY();
        if( _cursor_movement.x != std::nullopt && _cursor_movement.y != std::nullopt ) {
            m_Screen.GoTo( x + *_cursor_movement.x, y + *_cursor_movement.y );
        }
        else if( _cursor_movement.x != std::nullopt && _cursor_movement.y == std::nullopt ) {
            m_Screen.GoTo( x + *_cursor_movement.x, y );
        }
        else if( _cursor_movement.x == std::nullopt && _cursor_movement.y != std::nullopt ) {
            m_Screen.GoTo( x, y + *_cursor_movement.y );
        }
    }    
}

void InterpreterImpl::ProcessHT( signed _amount )
{
    if( _amount == 0 )
        return;
    else if( _amount > 0 ) {
        const int screen_width = m_Screen.Width();
        const int tab_stops_width = static_cast<int>(m_TabStops.size());
        const int width = std::min(screen_width, tab_stops_width);
        int x = m_Screen.CursorX();
        while( x < width - 1 && _amount > 0) {
            ++x;
            if( m_TabStops[x] )
                --_amount;
        }
        m_Screen.GoTo( x, m_Screen.CursorY() );                        
    }
    else if( _amount < 0 ) {
        int x = m_Screen.CursorX();
        while( x > 0 && _amount < 0) {
            --x;
            if( m_TabStops[x] )
                ++_amount;        
        }
        m_Screen.GoTo( x, m_Screen.CursorY() );
    }
}

void InterpreterImpl::ProcessReport( const input::DeviceReport _device_report )
{
    using input::DeviceReport;
    if( _device_report.mode == DeviceReport::TerminalId ) {
        // reporting our id as VT102
        const auto myid = "\033[?6c";
        Response(myid);
    }
    if( _device_report.mode == DeviceReport::DeviceStatus ) {
        const auto ok = "\033[0n";
        Response(ok);
    }
    if( _device_report.mode == DeviceReport::CursorPosition ) {    
// orig:    
//        sprintf(buf,
//                "\033[?%d;%dR",
//                (m_LineAbs ? m_Scr.CursorY() : m_Scr.CursorY() - m_Top) + 1,
//                m_Scr.CursorX() + 1
//                );
        char buf[64];
        const int x = m_Screen.CursorX();
        const int y = m_Screen.CursorY();
        sprintf(buf, "\033[%d;%dR", y + 1, x + 1 );
        Response(buf);
    }
}

void InterpreterImpl::ProcessBell()
{
    assert( m_Bell );
    m_Bell();
}

void InterpreterImpl::Response(std::string_view _text)
{
    assert( m_Output );
    Bytes bytes{reinterpret_cast<const std::byte*>(_text.data()), _text.length()}; 
    m_Output(bytes);
}

static std::u32string ConvertUTF8ToUTF32( std::string_view _utf8 )
{
    // temp and slow implementation
    auto str = base::CFPtr<CFStringRef>::adopt( CFStringCreateWithUTF8StringNoCopy( _utf8) ); 
    if( !str )
        return {};
    
    const auto utf16_len = CFStringGetLength(str.get());
    const auto utf32_len = CFStringGetBytes(str.get(),
                                            CFRangeMake(0, utf16_len),
                                            kCFStringEncodingUTF32LE,
                                            0,
                                            false,
                                            nullptr,
                                            0,
                                            nullptr);
    if( utf32_len == 0 )
        return {};
        
    std::u32string result;
    result.resize(utf32_len);
            
    const auto utf32_fact = CFStringGetBytes(str.get(),
                                            CFRangeMake(0, utf16_len),
                                            kCFStringEncodingUTF32LE,
                                            0,
                                            false,
                                            reinterpret_cast<UInt8*>(result.data()),
                                            result.size() * sizeof(char32_t),
                                            nullptr);
                                            
    assert( utf32_len == utf32_fact );

    return result; 
}

std::u32string ComposeUnicodePoints( std::u32string _utf32 )
{
    // temp and slow implementation
    const bool can_be_composed = std::any_of(_utf32.begin(), _utf32.end(), [](const char32_t _c){
        return oms::CanCharBeTheoreticallyComposed(_c);
    });
    if( can_be_composed == false )
        return _utf32;

    const auto orig_str = base::CFPtr<CFStringRef>::adopt(CFStringCreateWithBytesNoCopy(nullptr,
                                                                                        (UInt8*)_utf32.data(),
                                                                                        _utf32.length() * sizeof(char32_t),
                                                                                        kCFStringEncodingUTF32LE,
                                                                                        false,
                                                                                        kCFAllocatorNull) );
                                                                                            
    const auto mut_str = base::CFPtr<CFMutableStringRef>::adopt(CFStringCreateMutableCopy(nullptr,
                                                                                          0,
                                                                                          orig_str.get()) );
                                                                                              
    CFStringNormalize(mut_str.get(), kCFStringNormalizationFormC);
    const auto utf16_len = CFStringGetLength(mut_str.get());

    const auto utf32_len = CFStringGetBytes(mut_str.get(),
                                            CFRangeMake(0, utf16_len),
                                            kCFStringEncodingUTF32LE,
                                            0,
                                            false,
                                            nullptr,
                                            0,
                                            nullptr);
    if( utf32_len == 0 )
        return {};
        
    _utf32.resize(utf32_len);
            
    const auto utf32_fact = CFStringGetBytes(mut_str.get(),
                                            CFRangeMake(0, utf16_len),
                                            kCFStringEncodingUTF32LE,
                                            0,
                                            false,
                                            reinterpret_cast<UInt8*>(_utf32.data()),
                                            _utf32.size() * sizeof(char32_t),
                                            nullptr);
                                            
    assert( utf32_len == utf32_fact );        
    
    return _utf32;
}

void InterpreterImpl::ResetToDefaultTabStops(TabStops &_tab_stops)
{
    _tab_stops.reset();
    for( size_t n = 0; n < _tab_stops.size(); n += 8 )
        _tab_stops.set(n, true);
}

}