//
//  TermScreen.h
//  TermPlays
//
//  Created by Michael G. Kazakov on 17.11.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#pragma once

#include "TermScreenBuffer.h"

class TermScreen
{
public:
    TermScreen(int _w, int _h);
    ~TermScreen();

    static const unsigned short MultiCellGlyph = 0xFFFE;
    using Space = TermScreenBuffer::Space;
    
    inline void Lock()      { m_Lock.lock();   }
    inline void Unlock()    { m_Lock.unlock(); }
    
//    const Line *GetScreenLine(int _line_no) const;
//    const Line *GetScrollBackLine(int _line_no) const;
    
//    inline int ScrollBackLinesCount() const { return (int)m_ScrollBack.size(); }
    
    void ResizeScreen(int _new_sx, int _new_sy);
    
    void PutCh(uint32_t _char);
    void PutString(const string &_str);
    
    /**
     * Marks current screen line as wrapped. That means that the next line is continuation of current line.
     */
    void PutWrap();
    
    void SetFgColor(int _color);
    void SetBgColor(int _color);
    void SetIntensity(bool _intensity);
    void SetUnderline(bool _is_underline);
    void SetReverse(bool _is_reverse);
    void SetAlternateScreen(bool _is_alternate);

    void GoTo(int _x, int _y);
    void DoCursorUp(int _n = 1);
    void DoCursorDown(int _n = 1);
    void DoCursorLeft(int _n = 1);
    void DoCursorRight(int _n = 1);
    
    void ScrollDown(unsigned _top, unsigned _bottom, unsigned _lines);
    void DoScrollUp(unsigned _top, unsigned _bottom, unsigned _lines);
    
    void SaveScreen();
    void RestoreScreen();
    


    inline const TermScreenBuffer &Buffer() const { return m_Buffer; }
    inline int Width()   const { return /*m_Width*/ m_Buffer.Width();  }
    inline int Height()  const { return /*m_Height*/ m_Buffer.Height(); }
    
    
//    inline int Width()   const { return /*m_Width*/ m_Buffer->Width();  }
//    inline int Height()  const { return /*m_Height*/ m_Buffer->Height(); }
    inline int CursorX() const { return m_PosX;   }
    inline int CursorY() const { return m_PosY;   }
    
// CSI n J
// ED – Erase Display	Clears part of the screen.
//    If n is zero (or missing), clear from cursor to end of screen.
//    If n is one, clear from cursor to beginning of the screen.
//    If n is two, clear entire screen
    void DoEraseScreen(int _mode);

// CSI n K
// EL – Erase in Line	Erases part of the line.
// If n is zero (or missing), clear from cursor to the end of the line.
// If n is one, clear from cursor to beginning of the line.
// If n is two, clear entire line.
// Cursor position does not change.
    void EraseInLine(int _mode);
    
    // Erases _n characters in line starting from current cursor position. _n may be beyond bounds
    void EraseInLineCount(unsigned _n);
    
    void EraseAt(unsigned _x, unsigned _y, unsigned _count);
    
    
    void DoShiftRowLeft(int _chars);
    void DoShiftRowRight(int _chars);    
    
    inline void SetTitle(const char *_t) { strcpy(m_Title, _t); }
    inline const char* Title() const { return m_Title; }
    
private:
    void CopyLineChars(int _from, int _to);
    void ClearLine(int _ind);
//    struct ScreenShot // allocated with malloc, line by line from [0] till [height-1]
//    {
//        int width;
//        int height;
//        Space chars[1]; // chars will be a real size
//        static inline size_t sizefor(int _sx, int _sy) { return sizeof(int)*2 + sizeof(Space)*_sx*_sy; }
//    };
    
    mutex                         m_Lock;
    int                           m_ForegroundColor = TermScreenColors::Default;
    int                           m_BackgroundColor = TermScreenColors::Default;
    bool                          m_Intensity = false;
    bool                          m_Underline = false;
    bool                          m_Reverse = false;
    bool                          m_AlternateScreen = false;
//    int                           m_Width = 0;
//    int                           m_Height = 0;
    int                           m_PosX = 0;
    int                           m_PosY = 0;
    Space                         m_EraseChar;
//    ScreenShot                   *m_ScreenShot = nullptr;
    
    TermScreenBuffer              m_Buffer;
    
    // TODO: merge screen with scrollback to eliminate torn lines effects
//    list<Line>                    m_Screen;
//    list<Line>                    m_ScrollBack;

    
    static const int        m_TitleMaxLen = 1024;
    char                    m_Title[m_TitleMaxLen];
    
    
//    Line *GetLineRW(int _line_no);
//    static list<vector<Space>> ComposeContinuousLines(const list<Line> &_from);
//    static list<vector<Space>> ComposeContinuousLines(const list<Line> &_from1, const list<Line> &_from2);
//    static list<Line> DecomposeContinuousLines(const list<vector<Space>> &_from, unsigned _width);
};

