#pragma once

#ifdef RAPIDJSON_RAPIDJSON_H_
    #error include this file before rapidjson headers!
#endif

#define RAPIDJSON_48BITPOINTER_OPTIMIZATION   1
#define RAPIDJSON_SSE2 1
#define RAPIDJSON_HAS_STDSTRING 1

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/stringbuffer.h>
#include <VFS/VFSDeclarations.h>

@class NSData;
@class NSURL;
@class NSURLSession;
@class NSURLRequest;
@class NSURLResponse;
@class NSError;

namespace VFSNetDropbox
{
    struct api
    {
        static NSURL* const GetCurrentAccount;
        static NSURL* const GetSpaceUsage;
        static NSURL* const GetMetadata;
        static NSURL* const ListFolder;
        static NSURL* const Delete;
        static NSURL* const CreateFolder;
        static NSURL* const Move;
        static NSURL* const Download;
        static NSURL* const Upload;
        static NSURL* const UploadSessionStart;
        static NSURL* const UploadSessionAppend;
        static NSURL* const UploadSessionFinish;
    };

    void InsetHTTPBodyPathspec(NSMutableURLRequest *_request, const string &_path);
    void InsetHTTPHeaderPathspec(NSMutableURLRequest *_request, const string &_path);
    
    // returns VFSError and NSData, if VFSError is Ok
    pair<int, NSData *> SendSynchronousRequest(NSURLSession *_session,
                                               NSURLRequest *_request,
                                               const VFSCancelChecker &_cancel_checker = nullptr);

    struct Metadata
    {
        string name = ""; // will be empty on errors
        bool is_directory = false;
        int64_t size = -1;
        int64_t chg_time = -1;
    };
    Metadata ParseMetadata( const rapidjson::Value &_value );
    vector<Metadata> ExtractMetadataEntries( const rapidjson::Value &_value );
    
    
    struct AccountInfo
    {
        string accountid;
        string email;
        /* others later */
    };
    AccountInfo ParseAccountInfo( const rapidjson::Value &_value );
    
    const char *GetString( const rapidjson::Value &_doc, const char *_key );
    optional<long> GetLong( const rapidjson::Value &_doc, const char *_key );
    
    string EscapeString(const string &_original);
    string EscapeStringForJSONInHTTPHeader(const string &_original);
    
    
    optional<rapidjson::Document> ParseJSON( NSData *_data );
    
    bool IsNormalJSONResponse( NSURLResponse *_response );
    
    void WarnAboutUsingInMainThread();
    
};
