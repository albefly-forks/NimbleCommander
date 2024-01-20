// Copyright (C) 2013-2023 Michael Kazakov. Subject to GNU General Public License version 3.
#include "AppDelegate.h"
#include "AppDelegateCPP.h"
#include "AppDelegate+Migration.h"
#include "AppDelegate+MainWindowCreation.h"
#include "AppDelegate+ViewerCreation.h"
#include "ActivationManagerImpl.h"
#include "Config.h"
#include "ConfigWiring.h"
#include "VFSInit.h"
#include "Interactions.h"
#include "NCHelpMenuDelegate.h"
#include "SparkleShim.h"
#include "NativeVFSHostInstance.h"
#include "NCE.h"

#include "../../3rd_Party/NSFileManagerDirectoryLocations/NSFileManager+DirectoryLocations.h"
#include <LetsMove/PFMoveApplication.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <magic_enum.hpp>

#include <Base/CommonPaths.h>
#include <Base/CFDefaultsCPP.h>
#include <Base/algo.h>

#include <Utility/NSMenu+Hierarchical.h>
#include <Utility/NativeFSManagerImpl.h>
#include <Utility/TemporaryFileStorageImpl.h>
#include <Utility/PathManip.h>
#include <Utility/FunctionKeysPass.h>
#include <Utility/StringExtras.h>
#include <Utility/ObjCpp.h>
#include <Utility/UTIImpl.h>
#include <Utility/SystemInformation.h>
#include <Utility/Log.h>
#include <Utility/FSEventsFileUpdateImpl.h>
#include <Utility/SpdLogWindow.h>

#include <RoutedIO/RoutedIO.h>
#include <RoutedIO/Log.h>

#include <NimbleCommander/Core/ActionsShortcutsManager.h>
#include <NimbleCommander/Core/SandboxManager.h>
#include <NimbleCommander/Core/FeedbackManagerImpl.h>
#include <NimbleCommander/Core/AppStoreHelper.h>
#include <NimbleCommander/Core/Dock.h>
#include <NimbleCommander/Core/ServicesHandler.h>
#include <NimbleCommander/Core/ConfigBackedNetworkConnectionsManager.h>
#include <NimbleCommander/Core/ConnectionsMenuDelegate.h>
#include <NimbleCommander/Core/Theming/SystemThemeDetector.h>
#include <NimbleCommander/Core/Theming/ThemesManager.h>
#include <NimbleCommander/Core/Theming/Theme.h>
#include <NimbleCommander/Core/VFSInstanceManagerImpl.h>
#include <NimbleCommander/States/Terminal/ShellState.h>
#include <NimbleCommander/States/MainWindow.h>
#include <NimbleCommander/States/MainWindowController.h>
#include <NimbleCommander/States/FilePanels/MainWindowFilePanelState.h>
#include <NimbleCommander/States/FilePanels/ExternalEditorInfo.h>
#include <NimbleCommander/States/FilePanels/PanelViewLayoutSupport.h>
#include <NimbleCommander/States/FilePanels/FavoritesImpl.h>
#include <NimbleCommander/States/FilePanels/FavoritesWindowController.h>
#include <NimbleCommander/States/FilePanels/FavoritesMenuDelegate.h>
#include <NimbleCommander/States/FilePanels/Helpers/ClosedPanelsHistoryImpl.h>
#include <NimbleCommander/States/FilePanels/Helpers/RecentlyClosedMenuDelegate.h>
#include <NimbleCommander/Preferences/Preferences.h>
#include <NimbleCommander/GeneralUI/TrialWindowController.h>
#include <NimbleCommander/GeneralUI/VFSListWindowController.h>

#include <Operations/Pool.h>
#include <Operations/PoolEnqueueFilter.h>
#include <Operations/AggregateProgressTracker.h>

#include <Config/ConfigImpl.h>
#include <Config/ObjCBridge.h>
#include <Config/FileOverwritesStorage.h>
#include <Config/Executor.h>
#include <Config/Log.h>

#include <Viewer/History.h>
#include <Viewer/Log.h>
#include <Viewer/ViewerViewController.h>
#include <Viewer/InternalViewerWindowController.h>

#include <Term/Log.h>

#include <VFS/Log.h>

#include <VFSIcon/Log.h>

#include <Panel/Log.h>
#include <Panel/ExternalTools.h>

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace std::literals;
using namespace nc::bootstrap;
using nc::bootstrap::ActivationManager;

static std::optional<std::string> Load(const std::string &_filepath);

static auto g_ConfigDirPostfix = @"/Config/";
static auto g_StateDirPostfix = @"/State/";

static nc::config::ConfigImpl *g_Config = nullptr;
static nc::config::ConfigImpl *g_State = nullptr;
static nc::config::ConfigImpl *g_NetworkConnectionsConfig = nullptr;
static nc::utility::TemporaryFileStorageImpl *g_TemporaryFileStorage = nullptr;

static const auto g_ConfigForceFn = "general.alwaysUseFnKeysAsFunctional";
static const auto g_ConfigExternalToolsList = "externalTools.tools_v1";
static const auto g_ConfigLayoutsList = "filePanel.layout.layouts_v1";
static const auto g_ConfigSelectedTheme = "general.theme";
static const auto g_ConfigThemes = "themes";
static const auto g_ConfigExtEditorsList = "externalEditors.editors_v1";

nc::config::Config &GlobalConfig() noexcept
{
    assert(g_Config);
    return *g_Config;
}

nc::config::Config &StateConfig() noexcept
{
    assert(g_State);
    return *g_State;
}

static void ResetDefaults()
{
    const auto bundle_id = NSBundle.mainBundle.bundleIdentifier;
    [NSUserDefaults.standardUserDefaults removePersistentDomainForName:bundle_id];
    [NSUserDefaults.standardUserDefaults synchronize];
    g_Config->ResetToDefaults();
    g_State->ResetToDefaults();
    g_Config->Commit();
    g_State->Commit();
}

static void UpdateMenuItemsPlaceholders(int _tag)
{
    static const auto app_name =
        static_cast<NSString *>([NSBundle.mainBundle.infoDictionary objectForKey:@"CFBundleName"]);

    if( auto menu_item = [NSApp.mainMenu itemWithTagHierarchical:_tag] ) {
        auto title = menu_item.title;
        title = [title stringByReplacingOccurrencesOfString:@"{AppName}" withString:app_name];
        menu_item.title = title;
    }
}

static void UpdateMenuItemsPlaceholders(const char *_action)
{
    UpdateMenuItemsPlaceholders(ActionsShortcutsManager::Instance().TagFromAction(_action));
}

static void CheckDefaultsReset()
{
    const auto erase_mask =
        NSEventModifierFlagCapsLock | NSEventModifierFlagShift | NSEventModifierFlagOption | NSEventModifierFlagCommand;
    if( (NSEvent.modifierFlags & erase_mask) == erase_mask )
        if( AskUserToResetDefaults() ) {
            ResetDefaults();
            exit(0);
        }
}

template <typename Log>
static void AttachToSink(spdlog::level::level_enum _level, std::shared_ptr<spdlog::sinks::sink> _sink)
{
    Log::Set(std::make_shared<spdlog::logger>(Log::Name(), _sink));
    Log::Get().set_level(_level);
}

static std::span<nc::base::SpdLogger *const> Loggers() noexcept
{
    static const auto loggers = std::to_array({&nc::config::Log::Logger(),
                                               &nc::panel::Log::Logger(),
                                               &nc::routedio::Log::Logger(),
                                               &nc::term::Log::Logger(),
                                               &nc::utility::Log::Logger(),
                                               &nc::vfs::Log::Logger(),
                                               &nc::vfsicon::Log::Logger(),
                                               &nc::viewer::Log::Logger()});
    return loggers;
}

static void SetupLogs()
{
    spdlog::level::level_enum level = spdlog::level::off;
    const auto defaults = NSUserDefaults.standardUserDefaults;
    const auto args = [defaults volatileDomainForName:NSArgumentDomain];
    if( const auto arg_level = nc::objc_cast<NSString>([args objectForKey:@"NCLogLevel"]) ) {
        const auto casted = magic_enum::enum_cast<spdlog::level::level_enum>(arg_level.UTF8String);
        level = casted.value_or(spdlog::level::off);
    }

    if( level < spdlog::level::off ) {
        const auto stdout_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
        for( auto logger: Loggers() ) {
            logger->Get().sinks().emplace_back(stdout_sink);
            logger->Get().set_level(level);
        }
    }
}

static NCAppDelegate *g_Me = nil;

@interface NCAppDelegate ()

@property(nonatomic, readonly) nc::core::Dock &dock;

@property(nonatomic) IBOutlet NSMenu *recentlyClosedMenu;

@end

@interface NCViewerWindowDelegateBridge : NSObject <NCViewerWindowDelegate>

- (void)viewerWindowWillShow:(InternalViewerWindowController *)_window;
- (void)viewerWindowWillClose:(InternalViewerWindowController *)_window;

@end

@implementation NCAppDelegate {
    std::vector<NCMainWindowController *> m_MainWindows;
    std::vector<InternalViewerWindowController *> m_ViewerWindows;
    nc::spinlock m_ViewerWindowsLock;
    std::string m_SupportDirectory;
    std::string m_ConfigDirectory;
    std::string m_StateDirectory;
    std::vector<nc::config::Token> m_ConfigObservationTickets;
    AppStoreHelper *m_AppStoreHelper;
    upward_flag m_FinishedLaunching;
    std::shared_ptr<nc::panel::FavoriteLocationsStorageImpl> m_Favorites;
    NSMutableArray *m_FilesToOpen;
    NCViewerWindowDelegateBridge *m_ViewerWindowDelegateBridge;
    std::unique_ptr<nc::utility::NativeFSManager> m_NativeFSManager;
    std::shared_ptr<nc::vfs::NativeHost> m_NativeHost;
    std::unique_ptr<nc::bootstrap::ActivationManager> m_ActivationManager;
    std::unique_ptr<nc::utility::FSEventsFileUpdateImpl> m_FSEventsFileUpdate;
    nc::ops::PoolEnqueueFilter m_PoolEnqueueFilter;
    std::unique_ptr<ConfigWiring> m_ConfigWiring;
    std::unique_ptr<nc::SystemThemeDetector> m_SystemThemeDetector;
    std::unique_ptr<nc::ThemesManager> m_ThemesManager;
    NCSpdLogWindowController *m_LogWindowController;
}

@synthesize mainWindowControllers = m_MainWindows;
@synthesize configDirectory = m_ConfigDirectory;
@synthesize stateDirectory = m_StateDirectory;
@synthesize supportDirectory = m_SupportDirectory;
@synthesize appStoreHelper = m_AppStoreHelper;

- (id)init
{
    self = [super init];
    if( self ) {
        SetupLogs();
        g_Me = self;
        m_FilesToOpen = [[NSMutableArray alloc] init];
        m_ViewerWindowDelegateBridge = [[NCViewerWindowDelegateBridge alloc] init];
        m_FSEventsFileUpdate = std::make_unique<nc::utility::FSEventsFileUpdateImpl>();
        m_NativeFSManager = std::make_unique<nc::utility::NativeFSManagerImpl>();
        m_NativeHost = std::make_shared<nc::vfs::NativeHost>(*m_NativeFSManager, *m_FSEventsFileUpdate);
        m_ActivationManager = [self createActivationManager];
        [self checkMASReceipt];
        CheckDefaultsReset();
        m_SupportDirectory =
            EnsureTrailingSlash(NSFileManager.defaultManager.applicationSupportDirectory.fileSystemRepresentationSafe);
        [self setupConfigs];
        m_SystemThemeDetector = std::make_unique<nc::SystemThemeDetector>();
    }
    return self;
}

+ (NCAppDelegate *)me
{
    return g_Me;
}

static std::string InstalledAquaticLicensePath()
{
    return nc::AppDelegate::SupportDirectory() + nc::bootstrap::ActivationManagerImpl::DefaultLicenseFilename();
}

static std::string AquaticPrimePublicKey()
{
    std::string key;
    key += NCE(nc::env::aqp_pk_00);
    key += NCE(nc::env::aqp_pk_01);
    key += NCE(nc::env::aqp_pk_02);
    key += NCE(nc::env::aqp_pk_03);
    key += NCE(nc::env::aqp_pk_04);
    key += NCE(nc::env::aqp_pk_05);
    key += NCE(nc::env::aqp_pk_06);
    key += NCE(nc::env::aqp_pk_07);
    key += NCE(nc::env::aqp_pk_08);
    key += NCE(nc::env::aqp_pk_09);
    key += NCE(nc::env::aqp_pk_10);
    key += NCE(nc::env::aqp_pk_11);
    key += NCE(nc::env::aqp_pk_12);
    key += NCE(nc::env::aqp_pk_13);
    key += NCE(nc::env::aqp_pk_14);
    key += NCE(nc::env::aqp_pk_15);
    key += NCE(nc::env::aqp_pk_16);
    key += NCE(nc::env::aqp_pk_17);
    key += NCE(nc::env::aqp_pk_18);
    key += NCE(nc::env::aqp_pk_19);
    key += NCE(nc::env::aqp_pk_20);
    key += NCE(nc::env::aqp_pk_21);
    key += NCE(nc::env::aqp_pk_22);
    key += NCE(nc::env::aqp_pk_23);
    key += NCE(nc::env::aqp_pk_24);
    key += NCE(nc::env::aqp_pk_25);
    key += NCE(nc::env::aqp_pk_26);
    key += NCE(nc::env::aqp_pk_27);
    key += NCE(nc::env::aqp_pk_28);
    key += NCE(nc::env::aqp_pk_29);
    key += NCE(nc::env::aqp_pk_30);
    key += NCE(nc::env::aqp_pk_31);
    key += NCE(nc::env::aqp_pk_32);
    return key;
}

- (std::unique_ptr<nc::bootstrap::ActivationManager>)createActivationManager
{
    [[clang::no_destroy]] static auto ext_license_support =
        ActivationManagerBase::ExternalLicenseSupport{AquaticPrimePublicKey(), InstalledAquaticLicensePath()};
    [[clang::no_destroy]] static auto trial_period_support =
        ActivationManagerBase::TrialPeriodSupport{ActivationManagerImpl::DefaultsTrialExpireDate()};

    const ActivationManager::Distribution type =
#if defined(__NC_VERSION_FREE__)
        ActivationManager::Distribution::Free;
#elif defined(__NC_VERSION_PAID__)
        ActivationManager::Distribution::Paid;
#elif defined(__NC_VERSION_TRIAL__)
        ActivationManager::Distribution::Trial;
#else
#error Invalid build configuration - no version type specified
#endif

    return std::make_unique<nc::bootstrap::ActivationManagerImpl>(
        type, ext_license_support, trial_period_support);
}

- (void)applicationWillFinishLaunching:(NSNotification *) [[maybe_unused]] _notification
{
    RegisterAvailableVFS();

    [self feedbackManager];

    // Init themes manager
    m_ThemesManager = std::make_unique<nc::ThemesManager>(GlobalConfig(), g_ConfigSelectedTheme, g_ConfigThemes);
    // also hook up the appearance change notification with the global application appearance
    auto update_tm_appearance = [self] {
        m_ThemesManager->NotifyAboutSystemAppearanceChange(m_SystemThemeDetector->SystemAppearance());
    };
    auto update_app_appearance = [self] {
            [NSApp setAppearance:m_ThemesManager->SelectedTheme().Appearance()];
    };
    update_tm_appearance();
    update_app_appearance();
    // observe forever
    [[clang::no_destroy]] static auto token =
        m_ThemesManager->ObserveChanges(nc::ThemesManager::Notifications::Appearance, update_app_appearance);
    [[clang::no_destroy]] static auto token1 = m_SystemThemeDetector->ObserveChanges(update_tm_appearance);
    
    [self themesManager];
    [self favoriteLocationsStorage];

    [self updateMainMenuFeaturesByVersionAndState];

    // update menu with current shortcuts layout
    ActionsShortcutsManager::Instance().SetMenuShortCuts([NSApp mainMenu]);

    [self wireMenuDelegates];

    if( self.activationManager.Sandboxed() ) {
        auto &sm = SandboxManager::Instance();
        if( sm.Empty() ) {
            sm.AskAccessForPathSync(nc::base::CommonPaths::Home(), false);
            if( self.mainWindowControllers.empty() ) {
                auto ctrl = [self allocateDefaultMainWindow];
                [ctrl showWindow:self];
            }
        }
    }
}

- (void)wireMenuDelegates
{
    // set up menu delegates. do this via DI to reduce links to AppDelegate in whole codebase
    auto item_for_action = [](const char *_action) {
        auto tag = ActionsShortcutsManager::Instance().TagFromAction(_action);
        return [NSApp.mainMenu itemWithTagHierarchical:tag];
    };

    static auto layouts_delegate = [[PanelViewLayoutsMenuDelegate alloc] initWithStorage:*self.panelLayouts];
    item_for_action("menu.view.toggle_layout_1").menu.delegate = layouts_delegate;

    auto manage_fav_item = item_for_action("menu.go.favorites.manage");
    static auto favorites_delegate =
        [[FavoriteLocationsMenuDelegate alloc] initWithStorage:*self.favoriteLocationsStorage
                                             andManageMenuItem:manage_fav_item];
    manage_fav_item.menu.delegate = favorites_delegate;

    auto clear_freq_item = [NSApp.mainMenu itemWithTagHierarchical:14220];
    static auto frequent_delegate =
        [[FrequentlyVisitedLocationsMenuDelegate alloc] initWithStorage:*self.favoriteLocationsStorage
                                                       andClearMenuItem:clear_freq_item];
    clear_freq_item.menu.delegate = frequent_delegate;

    const auto connections_menu_item = item_for_action("menu.go.connect.network_server");
    static const auto conn_delegate = [[ConnectionsMenuDelegate alloc]
        initWithManager:[]() -> NetworkConnectionsManager & { return *g_Me.networkConnectionsManager; }];
    connections_menu_item.menu.delegate = conn_delegate;

    auto panels_locator = []() -> MainWindowFilePanelState * {
        if( auto wnd = nc::objc_cast<NCMainWindow>(NSApp.keyWindow) )
            if( auto ctrl = nc::objc_cast<NCMainWindowController>(wnd.delegate) )
                return ctrl.filePanelsState;
        return nil;
    };
    static const auto recently_closed_delegate =
        [[NCPanelsRecentlyClosedMenuDelegate alloc] initWithMenu:self.recentlyClosedMenu
                                                         storage:self.closedPanelsHistory
                                                   panelsLocator:panels_locator];
    (void)recently_closed_delegate;

    // These menus will have a submenu generated on the fly by according actions.
    // However, it's required for these menu items to always have submenus so that
    // Preferences can detect it and mark its hotkeys as readonly.
    // This solution is horrible but I can find a better one right now.
    item_for_action("menu.file.open_with_submenu").submenu = [NSMenu new];
    item_for_action("menu.file.always_open_with_submenu").submenu = [NSMenu new];

    // Set up a delegate for the Help menu
    static const auto help_delegate = [[NCHelpMenuDelegate alloc] init];
    auto help_menu_item = [NSApp.mainMenu itemWithTagHierarchical:17'000].menu;
    help_menu_item.delegate = help_delegate;
}

- (void)updateMainMenuFeaturesByVersionAndState
{
    static NSMenu *original_menu_state = [NSApp.mainMenu copy];

    // disable some features available in menu by configuration limitation
    auto tag_from_lit = [](const char *s) { return ActionsShortcutsManager::Instance().TagFromAction(s); };
    auto current_menuitem = [&](const char *s) { return [NSApp.mainMenu itemWithTagHierarchical:tag_from_lit(s)]; };
    auto initial_menuitem = [&](const char *s) {
        return [original_menu_state itemWithTagHierarchical:tag_from_lit(s)];
    };
    auto hide = [&](const char *s) {
        auto item = current_menuitem(s);
        item.alternate = false;
        item.hidden = true;
    };
    auto enable = [&](const char *_action, bool _enabled) {
        current_menuitem(_action).action = _enabled ? initial_menuitem(_action).action : nil;
    };
    auto &am = self.activationManager;

    // one-way items hiding
    if( !am.HasTerminal() ) {
        hide("menu.view.show_terminal");
        hide("menu.view.panels_position.move_up");
        hide("menu.view.panels_position.move_down");
        hide("menu.view.panels_position.showpanels");
        hide("menu.view.panels_position.focusterminal");
        hide("menu.file.feed_filename_to_terminal");
        hide("menu.file.feed_filenames_to_terminal");
    }
    if( am.ForAppStore() ) {
        hide("menu.nimble_commander.active_license_file");
        hide("menu.nimble_commander.purchase_license");
    }
    if( am.Type() != ActivationManager::Distribution::Free || am.UsedHadPurchasedProFeatures() ) {
        hide("menu.nimble_commander.purchase_pro_features");
        hide("menu.nimble_commander.restore_purchases");
    }
    if( am.Type() != ActivationManager::Distribution::Trial || am.UserHadRegistered() ) {
        hide("menu.nimble_commander.active_license_file");
        hide("menu.nimble_commander.purchase_license");
    }
    if( !am.HasRoutedIO() )
        hide("menu.nimble_commander.toggle_admin_mode");

    // reversible items disabling / enabling
    enable("menu.file.calculate_checksum", am.HasChecksumCalculation());
    enable("menu.file.find_with_spotlight", am.HasSpotlightSearch());
    enable("menu.go.processes_list", am.HasPSFS());
    enable("menu.go.connect.ftp", am.HasNetworkConnectivity());
    enable("menu.go.connect.sftp", am.HasNetworkConnectivity());
    enable("menu.go.connect.webdav", am.HasNetworkConnectivity());
    enable("menu.go.connect.lanshare", am.HasLANSharesMounting());
    enable("menu.go.connect.dropbox", am.HasNetworkConnectivity());
    enable("menu.go.connect.network_server", am.HasNetworkConnectivity());
    enable("menu.command.system_overview", am.HasBriefSystemOverview());
    enable("menu.command.file_attributes", am.HasUnixAttributesEditing());
    enable("menu.command.volume_information", am.HasDetailedVolumeInformation());
    enable("menu.command.batch_rename", am.HasBatchRename());
    enable("menu.command.internal_viewer", am.HasInternalViewer());
    enable("menu.command.compress_here", am.HasCompressionOperation());
    enable("menu.command.compress_to_opposite", am.HasCompressionOperation());
    enable("menu.command.link_create_soft", am.HasLinksManipulation());
    enable("menu.command.link_create_hard", am.HasLinksManipulation());
    enable("menu.command.link_edit", am.HasLinksManipulation());
    enable("menu.command.open_xattr", am.HasXAttrFS());
}

- (void)applicationDidFinishLaunching:(NSNotification *) [[maybe_unused]] _notification
{
    m_FinishedLaunching.toggle();

    if( self.mainWindowControllers.empty() )
        [self applicationOpenUntitledFile:NSApp]; // if there's no restored windows - we'll create a
                                                  // freshly new one

    NSApp.servicesProvider = self;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp registerServicesMenuSendTypes:@[NSFilenamesPboardType, (__bridge NSString *)kUTTypeFileURL]
                             returnTypes:@[]]; // pasteboard types provided by PanelController
#pragma clang diagnostic pop
    NSUpdateDynamicServices();

    // Since we have different app names (Nimble Commander and Nimble Commander Pro) and one
    // fixed menu, we have to emplace the right title upon startup in some menu elements.
    UpdateMenuItemsPlaceholders("menu.nimble_commander.about");
    UpdateMenuItemsPlaceholders("menu.nimble_commander.hide");
    UpdateMenuItemsPlaceholders("menu.nimble_commander.quit");
    UpdateMenuItemsPlaceholders(17000); // Menu->Help

    [self temporaryFileStorage]; // implicitly runs the background temp storage purging

    auto &am = self.activationManager;

    // Non-MAS version stuff below:
    if( !am.ForAppStore() ) {
        if( am.ShouldShowTrialNagScreen() ) // check if we should show a nag screen
            dispatch_to_main_queue_after(500ms, [self] { [self showTrialWindow]; });

        // setup Sparkle updater stuff
        NSMenuItem *item = [[NSMenuItem alloc] init];
        item.title = NSLocalizedString(@"Check for Updates...",
                                       "Menu item title for check if any Nimble Commander updates are available");
        item.target = NCBootstrapSharedSUUpdaterInstance();
        item.action = NCBootstrapSUUpdaterAction();
        [[NSApp.mainMenu itemAtIndex:0].submenu insertItem:item atIndex:1];
    }

    // initialize stuff related with in-app purchases
    if( am.Type() == ActivationManager::Distribution::Free ) {
        m_AppStoreHelper = [[AppStoreHelper alloc] initWithActivationManager:am feedbackManager:self.feedbackManager];
        m_AppStoreHelper.onProductPurchased = [=, &am]([[maybe_unused]] const std::string &_id) {
            if( am.ReCheckProFeaturesInAppPurchased() ) {
                [self updateMainMenuFeaturesByVersionAndState];
            }
        };
        dispatch_to_main_queue_after(500ms, [=] { [m_AppStoreHelper showProFeaturesWindowIfNeededAsNagScreen]; });
        
        if( am.UsedHadPurchasedProFeatures() ) {
            self.dock.SetBaseIcon( [NSImage imageNamed:@"Icon"] );
        }
    }

    // accessibility stuff for NonMAS version
    if( am.Type() == ActivationManager::Distribution::Trial && GlobalConfig().GetBool(g_ConfigForceFn) ) {
        nc::utility::FunctionalKeysPass::Instance().Enable();
    }

    if( am.Type() == ActivationManager::Distribution::Trial && am.UserHadRegistered() == false &&
        am.IsTrialPeriod() == false )
        self.dock.SetUnregisteredBadge(true);

#ifdef __NC_VERSION_TRIAL__
    if( !am.ForAppStore() )
        PFMoveToApplicationsFolderIfNecessary();
#endif

    m_ConfigWiring = std::make_unique<ConfigWiring>(GlobalConfig(), m_PoolEnqueueFilter);
    m_ConfigWiring->Wire();

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(windowWillClose:)
                                                 name:NSWindowWillCloseNotification
                                               object:nil];
}

- (void)setupConfigs
{
    assert(g_Config == nullptr && g_State == nullptr);
    auto fm = NSFileManager.defaultManager;

    NSString *config = [fm.applicationSupportDirectory stringByAppendingString:g_ConfigDirPostfix];
    if( ![fm fileExistsAtPath:config] )
        [fm createDirectoryAtPath:config withIntermediateDirectories:true attributes:nil error:nil];
    m_ConfigDirectory = config.fileSystemRepresentationSafe;

    NSString *state = [fm.applicationSupportDirectory stringByAppendingString:g_StateDirPostfix];
    if( ![fm fileExistsAtPath:state] )
        [fm createDirectoryAtPath:state withIntermediateDirectories:true attributes:nil error:nil];
    m_StateDirectory = state.fileSystemRepresentationSafe;

    const auto bundle = NSBundle.mainBundle;
    const auto config_defaults_path = [bundle pathForResource:@"Config" ofType:@"json"].fileSystemRepresentationSafe;
    const auto config_defaults = Load(config_defaults_path);
    if( config_defaults == std::nullopt ) {
        std::cerr << "Failed to read the main config file: " << config_defaults_path << std::endl;
        exit(-1);
    }

    const auto state_defaults_path = [bundle pathForResource:@"State" ofType:@"json"].fileSystemRepresentationSafe;
    const auto state_defaults = Load(state_defaults_path);
    if( state_defaults == std::nullopt ) {
        std::cerr << "Failed to read the state config file: " << state_defaults_path << std::endl;
        exit(-1);
    }

    const auto write_delay = std::chrono::seconds{30};
    const auto reload_delay = std::chrono::seconds{1};

    g_Config = new nc::config::ConfigImpl(
        *config_defaults,
        std::make_shared<nc::config::FileOverwritesStorage>(self.configDirectory + "Config.json"),
        std::make_shared<nc::config::DelayedAsyncExecutor>(write_delay),
        std::make_shared<nc::config::DelayedAsyncExecutor>(reload_delay));

    g_State = new nc::config::ConfigImpl(
        *state_defaults,
        std::make_shared<nc::config::FileOverwritesStorage>(self.stateDirectory + "State.json"),
        std::make_shared<nc::config::DelayedAsyncExecutor>(write_delay),
        std::make_shared<nc::config::DelayedAsyncExecutor>(reload_delay));

    g_NetworkConnectionsConfig = new nc::config::ConfigImpl(
        "",
        std::make_shared<nc::config::FileOverwritesStorage>(self.configDirectory + "NetworkConnections.json"),
        std::make_shared<nc::config::DelayedAsyncExecutor>(write_delay),
        std::make_shared<nc::config::DelayedAsyncExecutor>(reload_delay));

    atexit([] {
        // this callback is quite brutal, but works well. may need to find some more gentle approach
        g_Config->Commit();
        g_State->Commit();
        g_NetworkConnectionsConfig->Commit();
    });
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *) [[maybe_unused]] _app
{
    return NO;
}

+ (void)restoreWindowWithIdentifier:(NSString *)identifier
                              state:(NSCoder *) [[maybe_unused]] _state
                  completionHandler:(void (^)(NSWindow *, NSError *))completionHandler
{
    NSWindow *window = nil;
    if( [identifier isEqualToString:NCMainWindow.defaultIdentifier] )
        window = [g_Me allocateMainWindowRestoredBySystem].window;
    completionHandler(window, nil);
}

- (IBAction)onMainMenuNewWindow:(id) [[maybe_unused]] _sender
{
    auto ctrl = [self allocateMainWindowRestoredManually];
    [ctrl showWindow:self];
}

- (void)addMainWindow:(NCMainWindowController *)_wnd
{
    m_MainWindows.push_back(_wnd);
}

- (void)removeMainWindow:(NCMainWindowController *)_wnd
{
    auto it = find(begin(m_MainWindows), end(m_MainWindows), _wnd);
    if( it != end(m_MainWindows) )
        m_MainWindows.erase(it);
}

- (void)windowWillClose:(NSNotification *)aNotification
{
    if( auto main_wnd = nc::objc_cast<NCMainWindow>(aNotification.object) )
        if( auto main_ctrl = nc::objc_cast<NCMainWindowController>(main_wnd.delegate) ) {
            dispatch_to_main_queue([=] { [self removeMainWindow:main_ctrl]; });
        }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *) [[maybe_unused]] _sender
{
    bool has_running_ops = false;
    auto controllers = self.mainWindowControllers;
    for( const auto &wincont : controllers )
        if( !wincont.operationsPool.Empty() ) {
            has_running_ops = true;
            break;
        }
        else if( wincont.terminalState && wincont.terminalState.isAnythingRunning ) {
            has_running_ops = true;
            break;
        }

    if( has_running_ops ) {
        if( !AskToExitWithRunningOperations() )
            return NSTerminateCancel;

        for( const auto &wincont : controllers ) {
            wincont.operationsPool.StopAndWaitForShutdown();
            [wincont.terminalState terminate];
        }
    }

    // last cleanup before shutting down here:
    if( m_Favorites )
        m_Favorites->StoreData(StateConfig(), "filePanel.favorites");

    return NSTerminateNow;
}

- (IBAction)OnMenuSendFeedback:(id) [[maybe_unused]] _sender
{
    self.feedbackManager.EmailFeedback();
}

- (BOOL)applicationShouldOpenUntitledFile:(NSApplication *) [[maybe_unused]] _sender
{
    return true;
}

- (BOOL)applicationOpenUntitledFile:(NSApplication *)sender
{
    if( !m_FinishedLaunching )
        return false;

    if( !self.mainWindowControllers.empty() )
        return true;

    [self onMainMenuNewWindow:sender];

    return true;
}

- (bool)processLicenseFileActivation:(NSArray<NSString *> *)_filenames
{
    [[clang::no_destroy]] static const auto nc_license_extension = "."s + self.activationManager.LicenseFileExtension();

    if( _filenames.count != 1 )
        return false;

    for( NSString *pathstring in _filenames )
        if( auto fs = pathstring.fileSystemRepresentationSafe ) {
            if( self.activationManager.Type() == ActivationManager::Distribution::Trial ) {
                if( _filenames.count == 1 && std::filesystem::path(fs).extension() == nc_license_extension ) {
                    std::string p = fs;
                    dispatch_to_main_queue([=] { [self processProvidedLicenseFile:p]; });
                    return true;
                }
            }
        }
    return false;
}

- (void)drainFilesToOpen
{
    if( m_FilesToOpen.count == 0 )
        return;

    if( ![self processLicenseFileActivation:m_FilesToOpen] )
        self.servicesHandler.OpenFiles(m_FilesToOpen);

    [m_FilesToOpen removeAllObjects];
}

- (BOOL)application:(NSApplication *) [[maybe_unused]] _sender openFile:(NSString *)filename
{
    [m_FilesToOpen addObjectsFromArray:@[filename]];
    dispatch_to_main_queue_after(250ms, [] { [g_Me drainFilesToOpen]; });
    return true;
}

- (void)application:(NSApplication *) [[maybe_unused]] _sender openFiles:(NSArray<NSString *> *)filenames
{
    [m_FilesToOpen addObjectsFromArray:filenames];
    dispatch_to_main_queue_after(250ms, [] { [g_Me drainFilesToOpen]; });
    [NSApp replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

- (void)processProvidedLicenseFile:(const std::string &)_path
{
    const bool valid_and_installed = self.activationManager.ProcessLicenseFile(_path);
    if( valid_and_installed ) {
        ThankUserForBuyingALicense();
        [self updateMainMenuFeaturesByVersionAndState];
        self.dock.SetUnregisteredBadge(false);
    }
}

- (IBAction)OnActivateExternalLicense:(id) [[maybe_unused]] _sender
{
    if( auto path = AskUserForLicenseFile(self.activationManager) )
        [self processProvidedLicenseFile:*path];
}

- (IBAction)OnPurchaseExternalLicense:(id) [[maybe_unused]] _sender
{
    const auto url = [NSURL URLWithString:@"http://magnumbytes.com/redirectlinks/buy_license"];
    [NSWorkspace.sharedWorkspace openURL:url];
}

- (IBAction)OnPurchaseProFeaturesInApp:(id) [[maybe_unused]] _sender
{
    [m_AppStoreHelper showProFeaturesWindow];
}

- (IBAction)OnRestoreInAppPurchases:(id) [[maybe_unused]] _sender
{
    [m_AppStoreHelper askUserToRestorePurchases];
}

- (void)openFolderService:(NSPasteboard *)pboard userData:(NSString *)data error:(__strong NSString **)error
{
    self.servicesHandler.OpenFolder(pboard, data, error);
}

- (void)revealItemService:(NSPasteboard *)pboard userData:(NSString *)data error:(__strong NSString **)error
{
    self.servicesHandler.RevealItem(pboard, data, error);
}

- (void)OnPreferencesCommand:(id) [[maybe_unused]] _sender
{
    ShowPreferencesWindow();
}

- (IBAction)OnShowHelp:(id) [[maybe_unused]] _sender
{
    const auto url = [NSBundle.mainBundle URLForResource:@"Help" withExtension:@"pdf"];
    [NSWorkspace.sharedWorkspace openURL:url];
}

- (IBAction)onMainMenuPerformGoToProductForum:(id) [[maybe_unused]] _sender
{
    const auto url = [NSURL URLWithString:@"http://magnumbytes.com/forum/"];
    [NSWorkspace.sharedWorkspace openURL:url];
}

- (IBAction)OnMenuToggleAdminMode:(id) [[maybe_unused]] _sender
{
    using nc::routedio::RoutedIO;
    if( RoutedIO::Instance().Enabled() )
        RoutedIO::Instance().TurnOff();
    else {
        const auto turned_on = RoutedIO::Instance().TurnOn();
        if( !turned_on )
            WarnAboutFailingToAccessPrivilegedHelper();
    }

    self.dock.SetAdminBadge(RoutedIO::Instance().Enabled());
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    auto tag = item.tag;

    IF_MENU_TAG("menu.nimble_commander.toggle_admin_mode")
    {
        bool enabled = nc::routedio::RoutedIO::Instance().Enabled();
        item.title = enabled ? NSLocalizedString(@"Disable Admin Mode", "Menu item title for disabling an admin mode")
                             : NSLocalizedString(@"Enable Admin Mode", "Menu item title for enabling an admin mode");
        return true;
    }

    return true;
}

- (NCConfigObjCBridge *)config
{
    static auto global_config_bridge = [[NCConfigObjCBridge alloc] initWithConfig:*g_Config];
    return global_config_bridge;
}

- (nc::config::Config &)globalConfig
{
    assert(g_Config);
    return *g_Config;
}

- (nc::config::Config &)stateConfig
{
    assert(g_State);
    return *g_State;
}

- (nc::panel::ExternalToolsStorage &)externalTools
{
    [[clang::no_destroy]] static //
        nc::panel::ExternalToolsStorage storage{g_ConfigExternalToolsList, self.globalConfig};
    return storage;
}

- (const std::shared_ptr<nc::panel::PanelViewLayoutsStorage> &)panelLayouts
{
    [[clang::no_destroy]] static auto i = std::make_shared<nc::panel::PanelViewLayoutsStorage>(g_ConfigLayoutsList);
    return i;
}

- (nc::ThemesManager &)themesManager
{
    assert(m_ThemesManager);
    return *m_ThemesManager;
}

- (ExternalEditorsStorage &)externalEditorsStorage
{
    static auto i = new ExternalEditorsStorage(g_ConfigExtEditorsList, self.activationManager);
    return *i;
}

- (const std::shared_ptr<nc::panel::FavoriteLocationsStorage> &)favoriteLocationsStorage
{
    static std::once_flag once;
    std::call_once(once, [&] {
        using t = nc::panel::FavoriteLocationsStorageImpl;
        m_Favorites = std::make_shared<t>(StateConfig(), "filePanel.favorites");
    });

    [[clang::no_destroy]] static const std::shared_ptr<nc::panel::FavoriteLocationsStorage> inst = m_Favorites;
    return inst;
}

- (bool)askToResetDefaults
{
    if( AskUserToResetDefaults() ) {
        ResetDefaults();
        return true;
    }
    return false;
}

- (void)addInternalViewerWindow:(InternalViewerWindowController *)_wnd
{
    auto lock = std::lock_guard{m_ViewerWindowsLock};
    m_ViewerWindows.emplace_back(_wnd);
}

- (void)removeInternalViewerWindow:(InternalViewerWindowController *)_wnd
{
    auto lock = std::lock_guard{m_ViewerWindowsLock};
    auto i = std::find(std::begin(m_ViewerWindows), std::end(m_ViewerWindows), _wnd);
    if( i != std::end(m_ViewerWindows) )
        m_ViewerWindows.erase(i);
}

- (InternalViewerWindowController *)findInternalViewerWindowForPath:(const std::string &)_path
                                                              onVFS:(const VFSHostPtr &)_vfs
{
    auto lock = std::lock_guard{m_ViewerWindowsLock};
    auto i = std::find_if(std::begin(m_ViewerWindows), std::end(m_ViewerWindows), [&](auto v) {
        return v.internalViewerController.filePath == _path && v.internalViewerController.fileVFS == _vfs;
    });
    return i != std::end(m_ViewerWindows) ? *i : nil;
    return nil;
}

- (InternalViewerWindowController *)retrieveInternalViewerWindowForPath:(const std::string &)_path
                                                                  onVFS:(const std::shared_ptr<VFSHost> &)_vfs
{
    dispatch_assert_main_queue();
    if( auto window = [self findInternalViewerWindowForPath:_path onVFS:_vfs] )
        return window;
    auto viewer_factory = [](NSRect rc) { return [NCAppDelegate.me makeViewerWithFrame:rc]; };
    auto ctrl = [self makeViewerController];
    auto window = [[InternalViewerWindowController alloc] initWithFilepath:_path
                                                                        at:_vfs
                                                             viewerFactory:viewer_factory
                                                                controller:ctrl];
    window.delegate = m_ViewerWindowDelegateBridge;

    return window;
}

- (IBAction)onMainMenuPerformShowVFSListAction:(id) [[maybe_unused]] _sender
{
    static __weak VFSListWindowController *existing_window = nil;
    if( auto w = static_cast<VFSListWindowController *>(existing_window) )
        [w show];
    else {
        auto window = [[VFSListWindowController alloc] initWithVFSManager:self.vfsInstanceManager];
        [window show];
        existing_window = window;
    }
}

- (IBAction)onMainMenuPerformShowFavorites:(id) [[maybe_unused]] _sender
{
    static __weak FavoritesWindowController *existing_window = nil;
    if( auto w = static_cast<FavoritesWindowController *>(existing_window) ) {
        [w show];
        return;
    }
    auto storage = []() -> nc::panel::FavoriteLocationsStorage & { return *NCAppDelegate.me.favoriteLocationsStorage; };
    FavoritesWindowController *window = [[FavoritesWindowController alloc] initWithFavoritesStorage:storage];
    auto provide_panel = []() -> std::vector<std::pair<VFSHostPtr, std::string>> {
        std::vector<std::pair<VFSHostPtr, std::string>> panel_paths;
        for( const auto &ctr : NCAppDelegate.me.mainWindowControllers ) {
            auto state = ctr.filePanelsState;
            auto paths = state.filePanelsCurrentPaths;
            for( const auto &p : paths )
                panel_paths.emplace_back(std::get<1>(p), std::get<0>(p));
        }
        return panel_paths;
    };
    window.provideCurrentUniformPaths = provide_panel;

    [window show];
    existing_window = window;
}

- (const std::shared_ptr<NetworkConnectionsManager> &)networkConnectionsManager
{
    [[clang::no_destroy]] static const auto mgr =
        std::make_shared<ConfigBackedNetworkConnectionsManager>(*g_NetworkConnectionsConfig, self.nativeFSManager);
    [[clang::no_destroy]] static const std::shared_ptr<NetworkConnectionsManager> int_ptr = mgr;
    return int_ptr;
}

- (nc::ops::AggregateProgressTracker &)operationsProgressTracker
{
    [[clang::no_destroy]] static const auto apt = [] {
        const auto apt = std::make_shared<nc::ops::AggregateProgressTracker>();
        apt->SetProgressCallback([](double _progress) { g_Me.dock.SetProgress(_progress); });
        return apt;
    }();
    return *apt.get();
}

- (nc::core::Dock &)dock
{
    static const auto instance = new nc::core::Dock;
    return *instance;
}

- (nc::core::VFSInstanceManager &)vfsInstanceManager
{
    static const auto instance = new nc::core::VFSInstanceManagerImpl;
    return *instance;
}

- (const std::shared_ptr<nc::panel::ClosedPanelsHistory> &)closedPanelsHistory
{
    [[clang::no_destroy]] static const auto impl = std::make_shared<nc::panel::ClosedPanelsHistoryImpl>();
    [[clang::no_destroy]] static const std::shared_ptr<nc::panel::ClosedPanelsHistory> history = impl;
    return history;
}

- (NCMainWindowController *)windowForExternalRevealRequest
{
    NCMainWindowController *target_window = nil;
    for( NSWindow *wnd in NSApplication.sharedApplication.orderedWindows )
        if( auto wc = nc::objc_cast<NCMainWindowController>(wnd.windowController) )
            if( [wc.topmostState isKindOfClass:MainWindowFilePanelState.class] ) {
                target_window = wc;
                break;
            }

    if( !target_window )
        target_window = [self allocateDefaultMainWindow];

    if( target_window )
        [target_window.window makeKeyAndOrderFront:self];

    return target_window;
}

- (nc::core::ServicesHandler &)servicesHandler
{
    auto window_locator = [] { return [g_Me windowForExternalRevealRequest]; };
    [[clang::no_destroy]] static nc::core::ServicesHandler handler(window_locator, self.nativeHostPtr);
    return handler;
}

- (nc::utility::NativeFSManager &)nativeFSManager
{
    return *m_NativeFSManager;
}

- (void)showTrialWindow
{
    const auto expired =
        (self.activationManager.UserHadRegistered() == false) && (self.activationManager.IsTrialPeriod() == false);

    auto window = [[TrialWindowController alloc] init];
    window.isExpired = expired;
    __weak NCAppDelegate *weak_self = self;
    window.onBuyLicense = [weak_self] {
        if( auto self = weak_self ) {
            [self OnPurchaseExternalLicense:self];
        }
    };
    window.onActivate = [weak_self] {
        if( auto self = weak_self ) {
            [self OnActivateExternalLicense:self];
            if( self.activationManager.UserHadRegistered() == true )
                return true;
        }
        return false;
    };
    window.onQuit = [weak_self] {
        if( auto self = weak_self ) {
            const auto expired = (self.activationManager.UserHadRegistered() == false) &&
                                 (self.activationManager.IsTrialPeriod() == false);
            if( expired == true )
                dispatch_to_main_queue([] { [NSApp terminate:nil]; });
        }
    };
    [window show];
}

static void DoTemporaryFileStoragePurge()
{
    assert(g_TemporaryFileStorage != nullptr);
    const auto deadline = time(nullptr) - 60 * 60 * 24; // 24 hours back
    g_TemporaryFileStorage->Purge(deadline);

    dispatch_after(6h, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), DoTemporaryFileStoragePurge);
}

- (nc::utility::TemporaryFileStorage &)temporaryFileStorage
{
    const auto instance = [] {
        const auto base_dir = nc::base::CommonPaths::AppTemporaryDirectory();
        const auto prefix = nc::utility::GetBundleID() + ".tmp.";
        g_TemporaryFileStorage = new nc::utility::TemporaryFileStorageImpl(base_dir, prefix);
        dispatch_to_background(DoTemporaryFileStoragePurge);
        return g_TemporaryFileStorage;
    }();

    return *instance;
}

- (nc::viewer::History &)internalViewerHistory
{
    static const auto history_state_path = "viewer.history";
    static const auto instance = [] {
        auto inst = new nc::viewer::History(*g_Config, *g_State, history_state_path);
        auto center = NSNotificationCenter.defaultCenter;
        // Save the history upon application shutdown
        [center addObserverForName:NSApplicationWillTerminateNotification
                            object:nil
                             queue:nil
                        usingBlock:^([[maybe_unused]] NSNotification *_Nonnull note) {
                          inst->SaveToStateConfig();
                        }];
        return inst;
    }();
    return *instance;
}

- (nc::utility::UTIDB &)utiDB
{
    [[clang::no_destroy]] static nc::utility::UTIDBImpl uti_db;
    return uti_db;
}

- (nc::vfs::NativeHost &)nativeHost
{
    return *m_NativeHost;
}

- (const std::shared_ptr<nc::vfs::NativeHost> &)nativeHostPtr
{
    return m_NativeHost;
}

- (nc::utility::FSEventsFileUpdate &)fsEventsFileUpdate
{
    return *m_FSEventsFileUpdate;
}

- (nc::ops::PoolEnqueueFilter &)poolEnqueueFilter
{
    return m_PoolEnqueueFilter;
}

- (nc::bootstrap::ActivationManager &)activationManager
{
    return *m_ActivationManager;
}

- (nc::FeedbackManager &)feedbackManager
{
    static nc::FeedbackManager *instance = [self] {
        auto fm = new nc::FeedbackManagerImpl(*m_ActivationManager);
        atexit([] { instance->UpdateStatistics(); });
        return fm;
    }();
    return *instance;
}

- (void)checkMASReceipt
{
    if( self.activationManager.ForAppStore() ) {
        const auto path = NSBundle.mainBundle.appStoreReceiptURL.path;
        const auto exists = [NSFileManager.defaultManager fileExistsAtPath:path];
        if( !exists ) {
            std::cerr << "No receipt - exit the app with code 173" << std::endl;
            exit(173);
        }
    }
}

- (IBAction)onMainMenuShowLogs:(id)_sender
{
    if( m_LogWindowController == nil )
        m_LogWindowController = [[NCSpdLogWindowController alloc] initWithLogs:Loggers()];
    [m_LogWindowController showWindow:self];
}

@end

static std::optional<std::string> Load(const std::string &_filepath)
{
    std::ifstream in(_filepath, std::ios::in | std::ios::binary);
    if( !in )
        return std::nullopt;

    std::string contents;
    in.seekg(0, std::ios::end);
    contents.resize(in.tellg());
    in.seekg(0, std::ios::beg);
    in.read(&contents[0], contents.size());
    in.close();
    return contents;
}

@implementation NCViewerWindowDelegateBridge

- (void)viewerWindowWillShow:(InternalViewerWindowController *)_window
{
    [NCAppDelegate.me addInternalViewerWindow:_window];
}

- (void)viewerWindowWillClose:(InternalViewerWindowController *)_window
{
    [NCAppDelegate.me removeInternalViewerWindow:_window];
}

@end

namespace nc::bootstrap {

nc::vfs::NativeHost &NativeVFSHostInstance() noexcept
{
    assert(g_Me != nil);
    return NCAppDelegate.me.nativeHost;
}

}