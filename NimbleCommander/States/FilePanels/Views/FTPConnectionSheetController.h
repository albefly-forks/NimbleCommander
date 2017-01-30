//
//  FTPConnectionSheetController.h
//  Files
//
//  Created by Michael G. Kazakov on 17.05.14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#pragma once

#include <Utility/SheetController.h>
#include <NimbleCommander/Core/NetworkConnectionsManager.h>

@interface FTPConnectionSheetController : SheetController
@property (strong) NSString *title;
@property (strong) NSString *server;
@property (strong) NSString *username;
@property (strong) NSString *password;
@property (strong) NSString *path;
@property (strong) NSString *port;
@property (strong) IBOutlet NSPopUpButton *saved;
- (IBAction)OnSaved:(id)sender;
- (IBAction)OnConnect:(id)sender;
- (IBAction)OnClose:(id)sender;
- (void)fillInfoFromStoredConnection:(NetworkConnectionsManager::Connection)_conn;

@property (readonly, nonatomic) NetworkConnectionsManager::Connection result;

@end