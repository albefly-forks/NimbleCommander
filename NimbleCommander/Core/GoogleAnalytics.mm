// Copyright (C) 2016-2017 Michael Kazakov. Subject to GNU General Public License version 3.
#include <Utility/SystemInformation.h>
#include <NimbleCommander/Bootstrap/ActivationManager.h>
#include "GoogleAnalytics.h"

static void PostStartupInfo()
{
    nc::utility::SystemOverview so;
    if( nc::utility::GetSystemOverview(so) )
        GA().PostEvent("Init info", "Hardware", so.coded_model.c_str());
    
    NSString* lang = [NSLocale.autoupdatingCurrentLocale objectForKey:NSLocaleLanguageCode];
    GA().PostEvent( "Init info", "UI Language", lang.UTF8String );
}

static const char *TrackingID()
{
    switch( nc::core::ActivationManager::Type() ) {
        case nc::core::ActivationManager::Distribution::Trial:
            return NCE(nc::env::ga_nonmas_trial);
        case nc::core::ActivationManager::Distribution::Free:
            return NCE(nc::env::ga_mas_free);
        case nc::core::ActivationManager::Distribution::Paid:
            return NCE(nc::env::ga_mas_paid);
        default:
            return "";
    }
}

GoogleAnalytics& GA() noexcept
{
    static GoogleAnalytics *inst = []{
        const auto ga = new GoogleAnalytics( TrackingID() );
        if( ga->IsEnabled() )
            dispatch_to_background( PostStartupInfo );
        return ga;
    }();
    
    return *inst;
}
