//
//  TermShellTask_Tests.mm
//  Files
//
//  Created by Michael G. Kazakov on 08/07/14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#include <Habanero/CommonPaths.h>
#include "../../../Files Tests/tests_common.h"
#include "TermShellTask.h"
#include "../../common.h"

static void testMicrosleep(uint64_t _microseconds)
{
    if( dispatch_is_main_queue() ) {
        double secs = double(_microseconds) / USEC_PER_SEC;
        NSDate *when = [NSDate dateWithTimeIntervalSinceNow:secs];
        do {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:when];
            if ([when timeIntervalSinceNow] < 0.0)
                break;
        } while(1);
    }
    else
        this_thread::sleep_for(microseconds(_microseconds));
}

static string ToRealPath(const string &_from)
{
    int tfd = open(_from.c_str(), O_RDONLY);
    if(tfd == -1)
        return _from;
    char path_out[MAXPATHLEN];
    int ret = fcntl(tfd, F_GETPATH, path_out);
    close(tfd);
    if(ret == -1)
        return _from;
    return path_out;
}

@interface TermShellTask_Tests : XCTestCase
@end

@implementation TermShellTask_Tests

- (void)testBasic {
    TermShellTask shell;
    XCTAssert( shell.State() == TermShellTask::TaskState::Inactive );
    
    string cwd = CommonPaths::Home();
    shell.Launch(cwd.c_str(), 100, 100);
    testMicrosleep( microseconds(5s).count() );
    
    // check cwd
    XCTAssert( ToRealPath(shell.CWD()) == ToRealPath(cwd) );
    XCTAssert( shell.State() == TermShellTask::TaskState::Shell);
    
    // the only task is running is shell itself, and is not returned by ChildrenList
    XCTAssert( shell.ChildrenList().empty() );

    // test executing binaries within a shell
    shell.ExecuteWithFullPath("/usr/bin/top", nullptr);
    testMicrosleep( microseconds(1s).count() );
    XCTAssert( shell.ChildrenList().size() == 1 );
    XCTAssert( shell.ChildrenList()[0] == "top" );
    XCTAssert( shell.State() == TermShellTask::TaskState::ProgramExternal);
    
    // simulates user press Q to quit top
    shell.WriteChildInput("q", 1);
    testMicrosleep( microseconds(1s).count() );
    XCTAssert( shell.ChildrenList().empty() );
    XCTAssert( shell.State() == TermShellTask::TaskState::Shell);
  
    // check chdir
    cwd = CommonPaths::Home() + "Downloads/";
    shell.ChDir( cwd.c_str() );
    testMicrosleep( microseconds(1s).count() );
    XCTAssert( shell.CWD() == cwd );
    XCTAssert( shell.State() == TermShellTask::TaskState::Shell);
    
    // test chdir in the middle of some typing
    shell.WriteChildInput("ls ", 3);
    cwd = CommonPaths::Home();
    shell.ChDir( cwd.c_str() );
    testMicrosleep( microseconds(1s).count() );
    XCTAssert( shell.CWD() == cwd );
    XCTAssert( shell.State() == TermShellTask::TaskState::Shell);

    // check internal program state
    shell.WriteChildInput("top\r", 4);
    testMicrosleep( microseconds(1s).count() );
    XCTAssert( shell.ChildrenList().size() == 1 );
    XCTAssert( shell.ChildrenList()[0] == "top" );
    XCTAssert( shell.State() == TermShellTask::TaskState::ProgramInternal );

    // check termination
    shell.Terminate();
    XCTAssert( shell.ChildrenList().empty() );
    XCTAssert( shell.State() == TermShellTask::TaskState::Inactive );
    
    // check execution with short path in different directory
    shell.Launch(CommonPaths::Home().c_str(), 100, 100);
    testMicrosleep( microseconds(1s).count() );
    shell.Execute("top", "/usr/bin/", nullptr);
    testMicrosleep( microseconds(1s).count() );
    XCTAssert( shell.ChildrenList().size() == 1 );
    XCTAssert( shell.ChildrenList()[0] == "top" );
    XCTAssert( shell.State() == TermShellTask::TaskState::ProgramExternal );
    
    shell.Terminate();
    XCTAssert( shell.ChildrenList().empty() );
}

@end