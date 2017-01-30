//
//  SimpleComboBoxPersistentDataSource.h
//  Files
//
//  Created by Michael G. Kazakov on 15/06/15.
//  Copyright (c) 2015 Michael G. Kazakov. All rights reserved.
//

#pragma once

@interface SimpleComboBoxPersistentDataSource : NSObject<NSComboBoxDataSource>

- (instancetype)initWithPlistPath:(NSString*)path;
- (instancetype)initWithStateConfigPath:(const string&)path;

- (void)reportEnteredItem:(NSString*)item; // item can be nil

@end