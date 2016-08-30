//
//  FileMask.h
//  Files
//
//  Created by Michael G. Kazakov on 30.07.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#include <regex>

class FileMask
{
public:
    FileMask(const char* _mask);
    FileMask(const string &_mask);

    // will return false on empty names regardless of current file mask
    bool MatchName(const char *_name) const;
    bool MatchName(const string &_name) const;

    /**
     * Return true if there's no valid mask to match for.
     */
    bool IsEmpty() const;
    
    /**
     * Get current file mask.
     */
    const string& Mask() const;
    
    /**
     * Return true if _mask is a wildcard(s).
     * If it's a set of fixed names or a single word - return false.
     */
    static bool IsWildCard(const string &_mask);
    
    /**
     * Will try to convert _mask into a wildcard, by preffixing it's parts with "*." or with "*".
     * Return "" on errors.
     */
    static string ToExtensionWildCard(const string& _mask);

    /**
     * Will try to convert _mask into a wildcard, by preffixing it's parts with "*" and suffixing with "*".
     * Return "" on errors.
     */
    static string ToFilenameWildCard(const string& _mask);
    
private:
    vector< pair<optional<regex>, optional<string>>> m_Masks; // regexp or corresponding simple mask
    string m_Mask;
};
