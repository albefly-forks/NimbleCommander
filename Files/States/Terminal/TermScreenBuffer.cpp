//
//  TermScreenBuffer.cpp
//  Files
//
//  Created by Michael G. Kazakov on 30/06/15.
//  Copyright (c) 2015 Michael G. Kazakov. All rights reserved.
//

#include "TermScreenBuffer.h"

using _ = TermScreenBuffer;

static_assert( sizeof(_::Space) == 10 , "");

_::TermScreenBuffer(unsigned _width, unsigned _height):
    m_Width(_width),
    m_Height(_height)
{
    m_OnScreenSpaces = ProduceRectangularSpaces(m_Width, m_Height);
    m_OnScreenLines.resize(m_Height);
    FixupOnScreenLinesIndeces(begin(m_OnScreenLines), end(m_OnScreenLines), m_Width);
}

unique_ptr<_::Space[]>_::ProduceRectangularSpaces(unsigned _width, unsigned _height)
{
    return make_unique<Space[]>(_width*_height);
}

unique_ptr<_::Space[]> _::ProduceRectangularSpaces(unsigned _width, unsigned _height, Space _initial_char)
{
    auto p = ProduceRectangularSpaces(_width, _height);
    fill( &p[0], &p[_width*_height], _initial_char );
    return p;
}

void _::FixupOnScreenLinesIndeces(vector<LineMeta>::iterator _i, vector<LineMeta>::iterator _e, unsigned _width)
{
    unsigned start = 0;
    for( ;_i != _e; ++_i, start += _width) {
        _i->start_index = start;
        _i->line_length = _width;
    }
}

_::RangePair<const _::Space> _::LineFromNo(int _line_number) const
{
    if( _line_number >= 0 && _line_number < m_OnScreenLines.size() ) {
        auto &l = m_OnScreenLines[_line_number];
        assert( l.start_index + l.line_length <= m_Height*m_Width );
        
        return { &m_OnScreenSpaces[l.start_index],
                 &m_OnScreenSpaces[l.start_index + l.line_length] };
    }
    else if( _line_number < 0 && -_line_number <= m_BackScreenLines.size() ) {
        unsigned ind = unsigned((signed)m_BackScreenLines.size() + _line_number);
        auto &l = m_BackScreenLines[ind];
        assert( l.start_index + l.line_length <= m_BackScreenSpaces.size() );
        return { &m_BackScreenSpaces[l.start_index],
                 &m_BackScreenSpaces[l.start_index + l.line_length] };
    } else
        return {nullptr, nullptr};
}

_::RangePair<_::Space> _::LineFromNo(int _line_number)
{
    if( _line_number >= 0 && _line_number < m_OnScreenLines.size() ) {
        auto &l = m_OnScreenLines[_line_number];
        assert( l.start_index + l.line_length <= m_Height*m_Width );
        
        return { &m_OnScreenSpaces[l.start_index],
                 &m_OnScreenSpaces[l.start_index + l.line_length] };
    }
    else if( _line_number < 0 && -_line_number <= m_BackScreenLines.size() ) {
        unsigned ind = unsigned((signed)m_BackScreenLines.size() + _line_number);
        auto &l = m_BackScreenLines[ind];
        assert( l.start_index + l.line_length <= m_BackScreenSpaces.size() );
        return { &m_BackScreenSpaces[l.start_index],
                 &m_BackScreenSpaces[l.start_index + l.line_length] };
    }
    else
        return {nullptr, nullptr};
}

_::LineMeta *_::MetaFromLineNo( int _line_number )
{
    if( _line_number >= 0 && _line_number < m_OnScreenLines.size() )
        return &m_OnScreenLines[_line_number];
    else if( _line_number < 0 && -_line_number <= m_BackScreenLines.size() ) {
        unsigned ind = unsigned((signed)m_BackScreenLines.size() + _line_number);
        return &m_BackScreenLines[ind];
    }
    else
        return nullptr;
}

const _::LineMeta *_::MetaFromLineNo( int _line_number ) const
{
    if( _line_number >= 0 && _line_number < m_OnScreenLines.size() )
        return &m_OnScreenLines[_line_number];
    else if( _line_number < 0 && -_line_number <= m_BackScreenLines.size() ) {
        unsigned ind = unsigned((signed)m_BackScreenLines.size() + _line_number);
        return &m_BackScreenLines[ind];
    }
    else
        return nullptr;
}

string _::DumpScreenAsANSI() const
{
    string result;
    for( auto &l:m_OnScreenLines )
        for(auto *i = &m_OnScreenSpaces[l.start_index], *e = i + l.line_length; i != e; ++i)
            result += ( ( i->l >= 32 && i->l <= 127 ) ? (char)i->l : ' ');
    return result;
}

bool _::LineWrapped(int _line_number) const
{
    if(auto l = MetaFromLineNo(_line_number))
        return l->is_wrapped;
    return false;
}

void _::SetLineWrapped(int _line_number, bool _wrapped)
{
    if(auto l = MetaFromLineNo(_line_number))
        l->is_wrapped = _wrapped;
}

_::Space _::EraseChar() const
{
    return m_EraseChar;
}

void _::SetEraseChar(Space _ch)
{
    m_EraseChar = _ch;
}

_::Space _::DefaultEraseChar()
{
    Space sp;
    sp.l = 0;
    sp.c1 = 0;
    sp.c2 = 0;
    sp.foreground = TermScreenColors::Default;
    sp.background = TermScreenColors::Default;
    sp.intensity = 0;
    sp.underline = 0;
    sp.reverse   = 0;
    return sp;
}

// need real ocupied size
// need "anchor" here
void _::ResizeScreen(unsigned _new_sx, unsigned _new_sy, bool _merge_with_backscreen)
{
    if( _new_sx == 0 || _new_sy == 0)
        throw out_of_range("TermScreenBuffer::ResizeScreen - screen sizes can't be zero");

    
    auto fill_scr_from_declines = [=](vector< tuple<vector<_::Space>, bool> >::const_iterator _i,
                                      vector< tuple<vector<_::Space>, bool> >::const_iterator _e,
                                      size_t _l = 0){
        for( ; _i != _e; ++_i, ++_l ) {
            copy( begin(get<0>(*_i)), end(get<0>(*_i)), &m_OnScreenSpaces[ m_OnScreenLines[_l].start_index ] );
            m_OnScreenLines[_l].is_wrapped = get<1>(*_i);
        }
    };
    auto fill_bkscr_from_declines = [=](vector< tuple<vector<_::Space>, bool> >::const_iterator _i,
                                        vector< tuple<vector<_::Space>, bool> >::const_iterator _e){
        for( ; _i != _e; ++_i ) {
            LineMeta lm;
            lm.start_index = (int)m_BackScreenSpaces.size();
            lm.line_length = (int)get<0>(*_i).size();
            lm.is_wrapped = get<1>(*_i);
            m_BackScreenLines.emplace_back(lm);
            m_BackScreenSpaces.insert(end(m_BackScreenSpaces), begin(get<0>(*_i)), end(get<0>(*_i)) );
        }
    };
    
    if( _merge_with_backscreen ) {
        auto comp_lines = ComposeContinuousLines(-BackScreenLines(), Height());
        auto decomp_lines = DecomposeContinuousLines(comp_lines, _new_sx);
        
        m_BackScreenLines.clear();
        m_BackScreenSpaces.clear();
        if( decomp_lines.size() > _new_sy) {
            fill_bkscr_from_declines( begin(decomp_lines), end(decomp_lines) - _new_sy );
            
            m_OnScreenSpaces = ProduceRectangularSpaces(_new_sx, _new_sy, m_EraseChar);
            m_OnScreenLines.resize(_new_sy);
            FixupOnScreenLinesIndeces(begin(m_OnScreenLines), end(m_OnScreenLines), _new_sx);
            fill_scr_from_declines( end(decomp_lines) - _new_sy, end(decomp_lines) );
        }
        else {
            m_OnScreenSpaces = ProduceRectangularSpaces(_new_sx, _new_sy, m_EraseChar);
            m_OnScreenLines.resize(_new_sy);
            FixupOnScreenLinesIndeces(begin(m_OnScreenLines), end(m_OnScreenLines), _new_sx);
            fill_scr_from_declines( begin(decomp_lines), end(decomp_lines) );
        }
    }
    else {
        auto bkscr_decomp_lines = DecomposeContinuousLines(ComposeContinuousLines(-BackScreenLines(), 0),
                                                           _new_sx);
        m_BackScreenLines.clear();
        m_BackScreenSpaces.clear();
        fill_bkscr_from_declines( begin(bkscr_decomp_lines), end(bkscr_decomp_lines) );
        
        auto onscr_decomp_lines = DecomposeContinuousLines(ComposeContinuousLines(0, Height()),
                                                           _new_sx);
        m_OnScreenSpaces = ProduceRectangularSpaces(_new_sx, _new_sy, m_EraseChar);
        m_OnScreenLines.resize(_new_sy);
        FixupOnScreenLinesIndeces(begin(m_OnScreenLines), end(m_OnScreenLines), _new_sx);
        fill_scr_from_declines( begin(onscr_decomp_lines), min(begin(onscr_decomp_lines) + _new_sy, end(onscr_decomp_lines)) );
    }

    m_Width = _new_sx;
    m_Height = _new_sy;
}

void _::FeedBackscreen( const Space* _from, const Space* _to, bool _wrapped )
{
    // TODO: trimming and empty lines ?
    while( _from < _to ) {
        unsigned line_len = min( m_Width, unsigned(_to - _from) );
        
        m_BackScreenLines.emplace_back();
        m_BackScreenLines.back().start_index = (unsigned)m_BackScreenSpaces.size();
        m_BackScreenLines.back().line_length = line_len;
        m_BackScreenLines.back().is_wrapped = _wrapped ? true : (m_Width < _to - _from);
        m_BackScreenSpaces.insert(end(m_BackScreenSpaces),
                                  _from,
                                  _from + line_len);

        _from += line_len;
    }
}

static inline bool IsOccupiedChar( const _::Space &_s )
{
    return _s.l != 0;
}

unsigned _::OccupiedChars( const Space *_begin, const Space *_end )
{
    assert( _end >= _end );
    if( _end == _begin)
        return 0;

    unsigned len = 0;
    for( auto i = _end - 1; i >= _begin; --i ) // going backward
        if( IsOccupiedChar(*i) ) {
            len = (unsigned)(i - _begin + 1);
            break;
        }
    
    return len;
}

bool _::HasOccupiedChars( const Space *_begin, const Space *_end )
{
    assert( _end >= _end );
    for( ; _begin != _end; ++_begin ) // going forward
        if( IsOccupiedChar(*_begin) )
            return true;
    return false;;
}

unsigned _::OccupiedChars( int _line_no ) const
{
    if( auto l = LineFromNo(_line_no) )
        return OccupiedChars(begin(l), end(l));
    return 0;
}

bool _::HasOccupiedChars( int _line_no ) const
{
    if( auto l = LineFromNo(_line_no) )
        return HasOccupiedChars(begin(l), end(l));
    return false;
}

vector<vector<_::Space>> _::ComposeContinuousLines(int _from, int _to) const
{
    vector<vector<_::Space>> lines;

    for( bool continue_prev = false; _from < _to; ++_from) {
        auto source = LineFromNo(_from);
        if(!source)
            throw out_of_range("invalid bounds in TermScreen::Buffer::ComposeContinuousLines");
        
        if(!continue_prev)
            lines.emplace_back();
        auto &current = lines.back();
        
        current.insert(end(current),
                       begin(source),
                       begin(source) + OccupiedChars(begin(source),
                                                     end(source))
                       );
        continue_prev = LineWrapped(_from);
    }
    
    return lines;
}

vector< tuple<vector<_::Space>, bool> > _::DecomposeContinuousLines( const vector<vector<Space>>& _src, unsigned _width )
{
    if( _width == 0)
        throw invalid_argument("TermScreenBuffer::DecomposeContinuousLines width can't be zero");

    vector< tuple<vector<_::Space>, bool> > result;
    
    for( auto &l: _src ) {
        if( l.empty() ) // special case for CRLF-only lines
            result.emplace_back( make_tuple<vector<_::Space>, bool>({}, false) );

        for( size_t i = 0, e = l.size(); i < e; i += _width ) {
            auto t = make_tuple<vector<_::Space>, bool>({}, false);
            if( i + _width < e ) {
                get<0>(t).assign( begin(l) + i, begin(l) + i + _width );
                get<1>(t) = true;
            }
            else {
                get<0>(t).assign( begin(l) + i, end(l) );
            }
            result.emplace_back( move(t) );
        }
    }
    return result;
}

_::Snapshot::Snapshot(unsigned _w, unsigned _h):
    width(_w),
    height(_h),
    chars(make_unique<Space[]>( _w*_h))
{
}

void _::MakeSnapshot()
{
    if( !m_Snapshot || m_Snapshot->width != m_Width || m_Snapshot->height != m_Height )
        m_Snapshot = make_unique<Snapshot>( m_Width, m_Height );
    copy_n( m_OnScreenSpaces.get(), m_Width*m_Height, m_Snapshot->chars.get() );
}

void _::RevertToSnapshot()
{
    if( !HasSnapshot() )
        return;
    
    if( m_Height == m_Snapshot->height && m_Width == m_Snapshot->width ) {
        copy_n( m_Snapshot->chars.get(), m_Width*m_Height, m_OnScreenSpaces.get() );
    }
    else { // TODO: anchor?
        fill_n( m_OnScreenSpaces.get(), m_Width*m_Height, m_EraseChar );
        for( int y = 0, e = min(m_Snapshot->height, m_Height); y != e; ++y ) {
            copy_n( m_Snapshot->chars.get() + y*m_Snapshot->width,
                   min(m_Snapshot->width, m_Width),
                   m_OnScreenSpaces.get() + y*m_Width);
        }
    }
}

void _::DropSnapshot()
{
    m_Snapshot.reset();
}

optional<pair<int, int>> _::OccupiedOnScreenLines() const
{
    int first = numeric_limits<int>::max(),
         last = numeric_limits<int>::min();
    for( int i = 0, e = Height(); i < e; ++i )
        if( HasOccupiedChars(i) ) {
            first = min(first, i);
            last = max(last, i);
        }
    
    if( first > last )
        return nullopt;
    
    return make_pair(first, last + 1);
}