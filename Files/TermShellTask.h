//
//  TermShellTask.h
//  TermPlays
//
//  Created by Michael G. Kazakov on 15.11.13.
//  Copyright (c) 2013 Michael G. Kazakov. All rights reserved.
//

#pragma once

#import "TermTask.h"

class TermShellTask : public TermTask
{
public:
    ~TermShellTask();
    
    enum class TaskState {

        // initial state - shell is not initialized and is not running
        Inactive        = 0,

        // shell is running normally
        Shell           = 1,
        
        // a child program is running under shell, executed from it's command line
        ProgramInternal = 2,
        
        // a child program is running under shell, executed from Files' UI
        ProgramExternal = 3,
        
        // shell died
        Dead            = 4
    };

    inline void SetOnBashPrompt(void (^_)(const char*)) { m_OnBashPrompt = _; };
    
    // launches /bin/bash actually (hardcoded now)
    void Launch(const char *_work_dir, int _sx, int _sy);
    void Terminate();
    void ChDir(const char *_new_cwd);
    
    /**
     * executes a binary file in a directory using ./filename.
     * _at can be NULL. if it is the same as CWD - then ignored.
     * _parameters can be NULL. if they are not NULL - this string should be escaped in advance - this function doesn't convert is anyhow.
     */
    void Execute(const char *_short_fn, const char *_at, const char *_parameters);
    
    /**
     * executes a binary by a full path.
     * _parameters can be NULL.
     */
    void ExecuteWithFullPath(const char *_path, const char *_parameters);
    
    void ResizeWindow(int _sx, int _sy);
    
    
    void WriteChildInput(const void *_d, int _sz);
    
    
    
    inline TaskState State() const { return m_State; }
    
    /**
     * Current working directory. With trailing slash, in form: /Users/migun/.
     * Return string by value to minimize potential chance to get race condition.
     */
    string CWD() const;
    
    /**
     * returns a list of children excluding topmost shell (ie bash).
     */
    vector<string> ChildrenList();
    
    
    
    
    /**
     * Returns number of characters filled in _escaped.
     * If returned values equals to _buf_sz - then buffer was exhausted.
     * Returns -1 on any other errors.
     */
    static int EscapeShellFeed(const char *_feed, char *_escaped, size_t _buf_sz);
    
    
private:
    bool IsCurrentWD(const char *_what) const;
    void ProcessBashPrompt(const void *_d, int _sz);
    void SetState(TaskState _new_state);
    void ShellDied();
    void CleanUp();
    void ReadChildOutput();
    void (^m_OnBashPrompt)(const char *_cwd) = nil;

    volatile TaskState m_State = TaskState::Inactive;
    volatile int m_MasterFD = -1;
    volatile int m_ShellPID = -1;
    int m_CwdPipe[2] = {-1, -1};
    volatile bool m_TemporarySuppressed = false; // will give no output until the next bash prompt will show m_RequestedCWD path
    int m_TermSX = 0;
    int m_TermSY = 0;
    thread m_InputThread;
    string m_RequestedCWD = "";
    string m_CWD = "";
    volatile bool m_IsShuttingDown = false;
};
