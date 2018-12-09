// Copyright (C) 2016-2018 Michael Kazakov. Subject to GNU General Public License version 3.
#pragma once

#include <VFS/VFS.h>
#include <NimbleCommander/Core/NetworkConnectionsManager.h>
#include <Config/RapidJSON_fwd.h>

// these routines implicitly use the following components:
// 1. NetworkConnectionsManager

namespace nc::core {
    class VFSInstanceManager;
}

namespace nc::panel {

struct PersistentLocation
{
    bool is_native() const noexcept;
    bool is_network() const noexcept;
    std::vector<std::any> hosts;  // .front() is a deepest host, .back() is topmost
                        // empty hosts means using native vfs
    string path;
};

class PanelDataPersisency
{
public:
    PanelDataPersisency( const NetworkConnectionsManager &_conn_manager );

    static string MakeFootprintString( const PersistentLocation &_loc );
    static size_t MakeFootprintStringHash( const PersistentLocation &PersistentLocation );
    
    // NB! these functions theat paths as a directory regardless, and resulting path will
    // containt a trailing slash.
    static string MakeVerbosePathString( const PersistentLocation &_loc );
    static string MakeVerbosePathString( const VFSHost &_host, const string &_directory );

    static std::optional<PersistentLocation> EncodeLocation(const VFSHost &_vfs,
                                                            const string &_path );
 
    std::optional<NetworkConnectionsManager::Connection>
     ExtractConnectionFromLocation( const PersistentLocation &_location );
    
    // the following functions will return kNullType in case of error
    using json = nc::config::Value;
    static json EncodeVFSPath( const VFSHost &_vfs, const string &_path );
    static json EncodeVFSPath( const VFSListing &_listing );
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ these functions should be replaced by the following chain:
    // VFSHost+Path or VFSListing => PersistentLocation => JSON representation
    
    static std::optional<PersistentLocation> JSONToLocation( const json &_json );
    static json LocationToJSON( const PersistentLocation &_location );
    
    // LocationToJSON( *EncodeLocation(host, path) ) == EncodeVFSPath(host, path)
    
    // uses current state to retrieve existing vfs if possible
    static int CreateVFSFromLocation( const PersistentLocation &_state,
                                     VFSHostPtr &_host,
                                     core::VFSInstanceManager &_inst_mgr);
    
    static string GetPathFromState( const json &_state );
    
    
/**
{
 hosts_v1: [...]
 path: "/erere/rere/trtr"
}
*/
    static json EncodeVFSHostInfo( const VFSHost& _host );

    
/*
Host info:
{
type: "type", // VFSNativeHost::Tag, VFSPSHost::Tag, VFSArchiveHost::Tag, VFSArchiveUnRARHost::Tag, VFSXAttrHost::Tag, "network"
              // perhaps "archive" in the future, when more of them will come and some dedicated "ArchiveManager" will appear

// for xattr, archives
junction: "path"

// for network:
uuid: "uuid"
}
 
 */

private:
    const NetworkConnectionsManager &m_ConnectionsManager;
};

}
