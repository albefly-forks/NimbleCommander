//
//  FileSysAttrChangeOperation.h
//  Directories
//
//  Created by Michael G. Kazakov on 02.04.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#pragma once

#include <NimbleCommander/Operations/Operation.h>
#include <NimbleCommander/Operations/OperationDialogAlert.h>

struct FileSysAttrAlterCommand;

@interface FileSysAttrChangeOperation : Operation

// passing with ownership, operation will free it on finish
- (id)initWithCommand:(FileSysAttrAlterCommand)_command;

- (void)Update;

- (OperationDialogAlert *)DialogOnChmodError:(int)_error
                                  ForFile:(const char *)_path
                                 WithMode:(mode_t)_mode;

- (OperationDialogAlert *)DialogOnChflagsError:(int)_error
                                   ForFile:(const char *)_path
                                 WithFlags:(uint32_t)_flags;

- (OperationDialogAlert *)DialogOnChownError:(int)_error
                                     ForFile:(const char *)_path
                                         Uid:(uid_t)_uid
                                         Gid:(gid_t)_gid;

- (OperationDialogAlert *)DialogOnFileTimeError:(int)_error
                                        ForFile:(const char *)_path
                                       WithAttr:(u_int32_t)_attr
                                           Time:(timespec)_time;

- (OperationDialogAlert *)DialogOnOpendirError:(int)_error ForDir:(const char *)_path;

- (OperationDialogAlert *)DialogOnStatError:(int)_error ForPath:(const char *)_path;

@end