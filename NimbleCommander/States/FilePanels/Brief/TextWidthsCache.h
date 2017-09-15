#pragma once

namespace nc::panel::brief {

class TextWithdsCache
{
public:
    static TextWithdsCache& Instance();

    vector<short> Widths( const vector<reference_wrapper<const string>> &_strings, NSFont *_font );

private:
    struct Cache {
        unordered_map<string, short> widthds;
        spinlock lock;
        atomic_bool purge_scheduled{false};
    };
    
    TextWithdsCache();
    ~TextWithdsCache();
    Cache &ForFont(NSFont *_font);
    void PurgeIfNeeded(Cache &_cache);
    static void Purge(Cache &_cache);
    
    unordered_map<string, Cache> m_CachesPerFont;
    spinlock m_Lock;
};

}