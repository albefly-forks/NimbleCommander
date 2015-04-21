//
//  ModernPanelViewPresentation.h
//  Files
//
//  Created by Pavel Dogurevich on 11.05.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#import "PanelViewPresentation.h"
#import "PanelViewPresentationItemsColoringFilter.h"
#import "ObjcToCppObservingBridge.h"

@class PanelView;
class ModernPanelViewPresentationIconCache;
class IconsGenerator;
class ModernPanelViewPresentationHeader;
class ModernPanelViewPresentationItemsFooter;
class ModernPanelViewPresentationVolumeFooter;

struct ModernPanelViewPresentationItemsColoringFilter
{
    string                                      name;
    NSColor                                     *regular = NSColor.blackColor; // all others state text color
    NSColor                                     *focused = NSColor.blackColor; // focused text color
    PanelViewPresentationItemsColoringFilter    filter;
    NSDictionary *Archive() const;
    static ModernPanelViewPresentationItemsColoringFilter Unarchive(NSDictionary *_dict);
};

class ModernPanelViewPresentation : public PanelViewPresentation
{
public:
    ModernPanelViewPresentation();
    ~ModernPanelViewPresentation() override;
    
    void Draw(NSRect _dirty_rect) override;
    void OnFrameChanged(NSRect _frame) override;
    
    NSRect GetItemColumnsRect() override;
    int GetItemIndexByPointInView(CGPoint _point, PanelViewHitTest::Options _opt) override;
    
    int GetMaxItemsPerColumn() const override;
    
    
    double GetSingleItemHeight() override;
    
    NSRect ItemRect(int _item_index) const override;
    NSRect ItemFilenameRect(int _item_index) const override;
    void SetupFieldRenaming(NSScrollView *_editor, int _item_index) override;
    void SetQuickSearchPrompt(NSString *_text) override;
    
    NSString* FileSizeToString(const VFSListingItem &_dirent);
private:
    struct ColoringAttrs {
        NSDictionary *focused;
        NSDictionary *regular;
        NSDictionary *focused_size;
        NSDictionary *regular_size;
        NSDictionary *focused_time;
        NSDictionary *regular_time;
    };
    
    struct ItemLayout {
        NSRect whole_area       = { {0, 0}, {-1, -1}};
        NSRect filename_area    = { {0, 0}, {-1, -1}};
        NSRect filename_fact    = { {0, 0}, {-1, -1}};
        NSRect icon             = { {0, 0}, {-1, -1}};
        // time?
        // size?
        // date?
        // mb later
    };
    
    NSPoint ItemOrigin(int _item_index) const; // for not visible items return {0,0}
    ItemLayout LayoutItem(int _item_index) const;
    void CalculateLayoutFromFrame();
    void OnDirectoryChanged() override;
    void BuildGeometry();
    void BuildAppearance();
    const ColoringAttrs& AttrsForItem(const VFSListingItem& _item) const;
    
    NSFont *m_Font;
    double m_FontAscent;
    double m_FontHeight;
    double m_LineHeight; // full height of a row with gaps
    double m_LineTextBaseline;
    double m_SizeColumWidth;
    double m_DateColumnWidth;
    double m_TimeColumnWidth;
    
    bool m_IsLeft;
    
    NSSize m_Size;
    NSRect m_ItemsArea;
    int m_ItemsPerColumn;
    
    CGColorRef  m_RegularBackground;
    CGColorRef  m_OddBackground;
    CGColorRef  m_ActiveCursor;
    CGColorRef  m_InactiveCursor;
    CGColorRef  m_ColumnDividerColor;
    vector<ModernPanelViewPresentationItemsColoringFilter> m_ColoringRules;
    vector<ColoringAttrs> m_ColoringAttrs;
    
    static NSImage *m_SymlinkArrowImage;
    
    ObjcToCppObservingBlockBridge *m_GeometryObserver;
    ObjcToCppObservingBlockBridge *m_AppearanceObserver;
    
    shared_ptr<IconsGenerator> m_IconCache;
    unique_ptr<ModernPanelViewPresentationHeader> m_Header;
    unique_ptr<ModernPanelViewPresentationItemsFooter> m_ItemsFooter;
    unique_ptr<ModernPanelViewPresentationVolumeFooter> m_VolumeFooter;
};
