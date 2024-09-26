// Copyright (C) 2019-2024 Michael Kazakov. Subject to GNU General Public License version 3.
#include <VFSIcon/ExtensionsWhitelistImpl.h>
#include <algorithm>

namespace nc::vfsicon {

ExtensionsWhitelistImpl::ExtensionsWhitelistImpl(const nc::utility::UTIDB &_uti_db,
                                                 const std::vector<std::string> &_allowed_utis)
    : m_UTIDB(_uti_db), m_Allowed_UTIs(_allowed_utis)
{
}

ExtensionsWhitelistImpl::~ExtensionsWhitelistImpl() = default;

bool ExtensionsWhitelistImpl::AllowExtension(const std::string &_extension) const
{
    const std::lock_guard lock{m_WhitelistLock};

    if( const auto it = m_Whitelist.find(_extension); it != m_Whitelist.end() )
        return it->second;

    const auto uti = m_UTIDB.UTIForExtension(_extension);
    const bool allow = std::ranges::any_of(m_Allowed_UTIs, [&](const auto &allowed_uti) {
        return uti == allowed_uti || m_UTIDB.ConformsTo(uti, allowed_uti);
    });

    m_Whitelist[_extension] = allow;
    return allow;
}

} // namespace nc::vfsicon
