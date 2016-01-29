//
//  RoutedIO.cpp
//  Files
//
//  Created by Michael G. Kazakov on 29/11/14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#include <ServiceManagement/ServiceManagement.h>
#include <Security/Authorization.h>
#include <Security/AuthorizationDB.h>
#include <Habanero/CommonPaths.h>
#include "RoutedIO.h"
#include "Common.h"
#include "RoutedIOInterfaces.h"

static PosixIOInterface &IODirectCreateProxy();
static PosixIOInterface &IOWrappedCreateProxy();

PosixIOInterface &RoutedIO::Direct    = IODirectCreateProxy();
PosixIOInterface &RoutedIO::Default   = IOWrappedCreateProxy();
static const char *g_HelperLabel      = "info.filesmanager.Files.PrivilegedIOHelper";
static CFStringRef g_HelperLabelCF    = CFStringCreateWithUTF8StringNoCopy(g_HelperLabel);

static PosixIOInterface &IODirectCreateProxy() {
    static PosixIOInterfaceNative direct;
    return direct;
}

static PosixIOInterface &IOWrappedCreateProxy()
{
    if(configuration::version == configuration::Version::Full) {
        static PosixIOInterfaceRouted routed(RoutedIO::Instance());
        return routed;
    }
    else {
        static PosixIOInterfaceNative direct;
        return direct;
    }
}

static optional<vector<uint8_t>> ReadFile(const char *_path)
{
    int fd = open(_path, O_RDONLY);
    if(fd < 0)
        return nullopt;

    long size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    auto buf = vector<uint8_t>(size);
    
    uint8_t *buftmp = buf.data();
    uint64_t szleft = size;
    while(szleft) {
        ssize_t r = read(fd, buftmp, szleft);
        if(r < 0) {
            close(fd);
            return nullopt;
        }
        szleft -= r;
        buftmp += r;
    }
    
    close(fd);
    return move(buf);
}

static const char *InstalledPath()
{
    // need to check hardcoded path on 10.7, 10.8 and 10.9
    static string s = "/Library/PrivilegedHelperTools/"s + g_HelperLabel;
    return s.c_str();
}

static const char *BundledPath()
{
    static string s = CommonPaths::AppBundle() + "Contents/Library/LaunchServices/" + g_HelperLabel;
    return s.c_str();
}

RoutedIO::RoutedIO()
{
}

RoutedIO& RoutedIO::Instance()
{
    static auto inst = make_unique<RoutedIO>();
    return *inst;
}

bool RoutedIO::IsHelperInstalled()
{
    return access(InstalledPath(), R_OK) == 0;
}

bool RoutedIO::IsHelperCurrent()
{
    auto inst = ReadFile(InstalledPath());
    auto bund = ReadFile(BundledPath());
    
    if(!inst || !bund)
        return false;
    
    if(inst->size() != bund->size())
        return false;
    
    return memcmp(inst->data(), bund->data(), inst->size()) == 0;
}

bool RoutedIO::TurnOn()
{
    if(configuration::version < configuration::Version::Full)
        return false;
    
    if(m_Enabled)
        return true;
    
    if( !IsHelperInstalled() ) {
        if( !AskToInstallHelper() )
            return false;
    }
    
    if(!AuthenticateAsAdmin())
        return false;
        
    if( !IsHelperCurrent() ) {
        // we have another version of a helper app
        if( Connect() && IsHelperAlive() ) {
            // ask helper it remove itself and then to exit gracefully
            xpc_connection_t connection = m_Connection;
            
            xpc_object_t message = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(message, "operation", "uninstall");
            xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, message);
            xpc_release(message);
            xpc_release(reply);
            
            message = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(message, "operation", "exit");
            xpc_connection_send_message_with_reply(connection, message, dispatch_get_main_queue(), ^(xpc_object_t event) {});
            xpc_release(message);
            
            xpc_release(m_Connection);
            m_Connection = nullptr;
            
            if( !AskToInstallHelper() )
                return false;
        }
        else {
            // helper is not current we can't ask it to remove itself. protocol/signing probs?
            // anywhay, can go this way, no turning routing on
            return false;
        }
    }
        
    if(Connect())
        m_Enabled = true;
    
    return m_Enabled;
}

void RoutedIO::TurnOff()
{
    if(m_Connection) {
        xpc_connection_cancel(m_Connection);
        xpc_release(m_Connection);
        m_Connection = nullptr;
    }
    m_Enabled = false;
    m_AuthenticatedAsAdmin = false;
}

bool RoutedIO::SayImAuthenticated(xpc_connection_t _connection)
{
    xpc_object_t message = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_bool(message, "auth", m_AuthenticatedAsAdmin);
    
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(_connection, message);
    xpc_release(message);
    
    bool result = false;
    if(xpc_get_type(reply) != XPC_TYPE_ERROR)
        if( xpc_dictionary_get_bool(reply, "ok") == true )
            result = true;
    
    xpc_release(reply);
    return result;
}

bool RoutedIO::AskToInstallHelper()
{
    if(configuration::version < configuration::Version::Full)
        return false;
    
    AuthorizationItem   authItem   = { kSMRightBlessPrivilegedHelper, 0, NULL, 0 };
    AuthorizationRights authRights = { 1, &authItem };
    AuthorizationFlags  flags      = kAuthorizationFlagInteractionAllowed|kAuthorizationFlagPreAuthorize|kAuthorizationFlagExtendRights;
    AuthorizationRef    authRef    = NULL;
    
    /* Obtain the right to install privileged helper tools (kSMRightBlessPrivilegedHelper). */
    OSStatus status = AuthorizationCreate(&authRights, kAuthorizationEmptyEnvironment, flags, &authRef);
    if (status != errAuthorizationSuccess)
        return false;

    m_AuthenticatedAsAdmin = true;
    CFErrorRef error;
    
    bool result = SMJobBless(kSMDomainSystemLaunchd, g_HelperLabelCF, authRef, &error);
    
    AuthorizationFree(authRef, kAuthorizationFlagDefaults);
    
    return result;
}

bool RoutedIO::AuthenticateAsAdmin()
{
    if(configuration::version < configuration::Version::Full)
        return false;
    
    if(m_AuthenticatedAsAdmin)
        return true;
    
    AuthorizationItem authItem     = { kAuthorizationRuleAuthenticateAsAdmin, 0, NULL, 0 };
    AuthorizationRights authRights = { 1, &authItem };
    AuthorizationFlags flags       = kAuthorizationFlagInteractionAllowed|kAuthorizationFlagPreAuthorize|kAuthorizationFlagExtendRights;
    
    AuthorizationRef authRef = NULL;
    OSStatus status = AuthorizationCreate(&authRights, kAuthorizationEmptyEnvironment, flags, &authRef);
    
    if(status == errAuthorizationSuccess)
        m_AuthenticatedAsAdmin = true;
    
    return status == errAuthorizationSuccess;
}

bool RoutedIO::Connect()
{
    if(m_Connection)
        return true;
    
    xpc_connection_t connection = xpc_connection_create_mach_service(g_HelperLabel, NULL, XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
    if(!connection)
        return false;
    
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            if (event == XPC_ERROR_CONNECTION_INVALID) {
//                xpc_release(connection);
                m_Connection = nullptr;
            }
        }
    });
    
    xpc_connection_resume(connection);

    if(!SayImAuthenticated(connection)) {
        xpc_connection_cancel(connection);
        return false;
    }
    
    m_Connection = connection;
    return true;
}

bool RoutedIO::ConnectionAvailable()
{
    return m_Connection != nullptr;
}

xpc_connection_t RoutedIO::Connection()
{
    if(ConnectionAvailable())
        return m_Connection;
    if(Connect())
        return m_Connection;
    return nullptr;
}

bool RoutedIO::IsHelperAlive()
{
    xpc_connection_t connection = Connection();
    if(!connection)
        return false;
    
    xpc_object_t message = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(message, "operation", "heartbeat");
    
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, message);
    xpc_release(message);
    
    bool result = false;
    if(xpc_get_type(reply) != XPC_TYPE_ERROR)
        if( xpc_dictionary_get_bool(reply, "ok") == true )
            result = true;
    
    xpc_release(reply);
    return result;
}
