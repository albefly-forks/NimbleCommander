#include "VFSNetDropboxFile.h"
#include "Aux.h"
#include "VFSNetDropboxFileUploadStream.h"
#include "VFSNetDropboxFileUploadDelegate.h"

using namespace VFSNetDropbox;

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                  ****** VFSNetDropboxFileDownloadDelegate ******
//
////////////////////////////////////////////////////////////////////////////////////////////////////
@interface VFSNetDropboxFileDownloadDelegate : NSObject<NSURLSessionDelegate>

- (instancetype)initWithFile:(shared_ptr<VFSNetDropboxFile>)_file;

@end

@implementation VFSNetDropboxFileDownloadDelegate
{
    weak_ptr<VFSNetDropboxFile> m_File;
}

- (instancetype)initWithFile:(shared_ptr<VFSNetDropboxFile>)_file
{
    if( self = [super init] ) {
        m_File = _file;
    }
    return self;
}

- (void)URLSession:(NSURLSession *)session didBecomeInvalidWithError:(nullable NSError *)error
{
    cout << "didBecomeInvalidWithError" << endl;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
                           didCompleteWithError:(nullable NSError *)error
{
    cout << "didCompleteWithError" << endl;
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask
                                     didReceiveData:(NSData *)data
{
//    cout << "didReceiveData" << endl;
    if( auto file = m_File.lock() )
        file->AppendDownloadedData(data);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask
                                 didReceiveResponse:(NSURLResponse *)response
                                  completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler
{
    cout << "didReceiveResponse" << endl;
    bool permit = false;
    if( auto file = m_File.lock() )
        permit = file->ProcessDownloadResponse(response);

    completionHandler( permit ?  NSURLSessionResponseAllow : NSURLSessionResponseCancel );
}
                                
@end

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//                  ****** VFSNetDropboxFile ******
//
////////////////////////////////////////////////////////////////////////////////////////////////////
VFSNetDropboxFile::VFSNetDropboxFile(const char* _relative_path,
                                     const shared_ptr<VFSNetDropboxHost> &_host):
    VFSFile(_relative_path, _host)
{
}

VFSNetDropboxFile::~VFSNetDropboxFile()
{
    Close();
}

int VFSNetDropboxFile::Close()
{
    int rc = VFSError::Ok;
    if( m_Upload ) {
        if( m_State == Uploading ) {
            if( m_Upload->upload_size == m_FilePos )
                [m_Upload->stream notifyAboutDataEnd];
            else {
                // client hasn't provided enough data and is closing a file.
                // this is an invalid behaviour, need to cancel a transfer task
                [m_Upload->task cancel];
                m_State = Canceled;
                rc = VFSError::FromErrno(EIO);
            }
            
            // need to wait for response from server before returning from Close();
            unique_lock<mutex> lk(m_SignalLock);
            m_Signal.wait(lk, [&]{ return m_State == Completed || m_State == Canceled; } );
        }
    }

    LOCK_GUARD(m_DataLock) {
        if( m_Download ) {
            [m_Download->task cancel];
            m_Download.reset();
        }
        
        if( m_Upload ) {
            m_Upload->stream.feedData = nullptr;
            m_Upload->stream.hasDataToFeed = nullptr;
            m_Upload->delegate.handleFinished = nullptr;
            m_Upload.reset();
        }       
    }
    
    m_FilePos   = 0;
    m_FileSize  = -1;
    m_State     = Cold;

    return rc;
}

int VFSNetDropboxFile::Open(int _open_flags, VFSCancelChecker _cancel_checker)
{
    if( m_Upload || m_Download )
        return VFSError::InvalidCall;

    auto &host = *((VFSNetDropboxHost*)Host().get());
    if( (_open_flags & VFSFlags::OF_Read) == VFSFlags::OF_Read ) {
        auto delegate = [[VFSNetDropboxFileDownloadDelegate alloc] initWithFile:
                         static_pointer_cast<VFSNetDropboxFile>(shared_from_this())];
        auto session = [NSURLSession sessionWithConfiguration:NSURLSessionConfiguration.defaultSessionConfiguration
                                                     delegate:delegate
                                                delegateQueue:nil];
        
        NSMutableURLRequest *req = [[NSMutableURLRequest alloc] initWithURL:api::Download];
        req.HTTPMethod = @"POST";
        host.FillAuth(req);
        InsetHTTPHeaderPathspec(req, RelativePath());
        
        m_State = Initiated;
        
        m_Download = make_unique<Download>();
        
        m_Download->task = [session dataTaskWithRequest:req];
        [m_Download->task resume];
        
        // wait for initial responce from dropbox
        unique_lock<mutex> lk(m_SignalLock);
        m_Signal.wait(lk, [=]{ return m_State != Initiated; } );
        
        return m_State == Downloading ? VFSError::Ok : VFSError::GenericError;
        
    }
    if( (_open_flags & VFSFlags::OF_Write) == VFSFlags::OF_Write ) {
        m_State = Initiated;
        m_Upload = make_unique<Upload>();
        // at this point we need to wait for SetUploadSize() call to build of a request
        // and to actually start it
        return VFSError::Ok;
    }
    
    return VFSError::InvalidCall;
}

VFSNetDropboxFile::ReadParadigm VFSNetDropboxFile::GetReadParadigm() const
{
    return ReadParadigm::Sequential;
}

VFSNetDropboxFile::WriteParadigm VFSNetDropboxFile::GetWriteParadigm() const
{
    return WriteParadigm::Upload;
}

void VFSNetDropboxFile::AppendDownloadedData( NSData *_data )
{
    if(!_data ||
        _data.length == 0 ||
        m_State != Downloading ||
        !m_Download ||
        m_FileSize < 0)
        return;
    
    LOCK_GUARD(m_DataLock) {
        [_data enumerateByteRangesUsingBlock:[=](const void *bytes, NSRange byteRange, BOOL *stop){
//            cout << "accepted bytes: " << byteRange.length << endl;
            m_Download->fifo.insert(end(m_Download->fifo),
                                  (const uint8_t*)bytes,
                                  (const uint8_t*)bytes + byteRange.length);
        }];
        
        if( m_Download->fifo_offset + m_Download->fifo.size() == m_FileSize )
            m_State = Completed;
    }
    
    LOCK_GUARD(m_SignalLock) {
        m_Signal.notify_all();
    }
}

bool VFSNetDropboxFile::ProcessDownloadResponse( NSURLResponse *_response )
{
    if( m_State != Initiated )
        return false;

    if( auto http_resp = objc_cast<NSHTTPURLResponse>(_response) ) {
        if( http_resp.statusCode == 200 ) {
            if( auto cl = objc_cast<NSString>(http_resp.allHeaderFields[@"Content-Length"]) ) {
                auto file_size = atol( cl.UTF8String );
                m_FileSize = file_size;
                
                m_State = Downloading;
                LOCK_GUARD(m_SignalLock) {
                    m_Signal.notify_all();
                }
                return true;
            }
        }
        else {
            NSLog(@"%@", _response);
        }
    }
    
    m_State = Canceled;
    LOCK_GUARD(m_SignalLock) {
        m_Signal.notify_all();
    }
    return false;
}

ssize_t VFSNetDropboxFile::Pos() const
{
    return 0;
}

ssize_t VFSNetDropboxFile::Size() const
{
    return m_FileSize >= 0 ? m_FileSize : VFSError::InvalidCall;
}

bool VFSNetDropboxFile::Eof() const
{
    return m_FilePos == m_FileSize;
}

ssize_t VFSNetDropboxFile::Read(void *_buf, size_t _size)
{
    if( m_State != Downloading && m_State != Completed )
        return VFSError::InvalidCall;
    if( !m_Download )
        return VFSError::InvalidCall;

    if( _size == 0 )
        return 0;
    
    if( Eof() )
        return 0;
    
    do {
        LOCK_GUARD(m_DataLock) {
            if( !m_Download->fifo.empty() ) {
                ssize_t sz = min( _size, m_Download->fifo.size() );
                copy_n( begin(m_Download->fifo), sz, (uint8_t*)_buf );
                m_Download->fifo.erase( begin(m_Download->fifo), begin(m_Download->fifo) + sz );
                m_Download->fifo_offset += sz;
                m_FilePos += sz;
                return sz;
            }
        }
    
        unique_lock<mutex> lk(m_SignalLock);
        m_Signal.wait(lk);
    } while( m_State == Downloading || m_State == Completed );
    return VFSError::GenericError;
}

bool VFSNetDropboxFile::IsOpened() const
{
    return m_State == Initiated ||
            m_State == Downloading ||
            m_State == Uploading ||
            m_State == Completed;
}

int VFSNetDropboxFile::PreferredIOSize() const
{
    return 32768; // packets are usually 16384 bytes long, use IO twice as long
}

int VFSNetDropboxFile::StartSmallUpload()
{
    assert( m_Upload != nullptr );
    assert( m_Upload->upload_size >= 0 && m_Upload->upload_size <= m_ChunkSize );
    assert( m_Upload->request == nil );
    assert( m_Upload->delegate == nil );
    assert( m_Upload->stream == nil );
    assert( m_Upload->task == nil );

    auto stream = [[VFSNetDropboxFileUploadStream alloc] init];
    stream.hasDataToFeed = [this]() -> bool {
        return HasDataToFeedUploadTask();
    };
    stream.feedData = [this](uint8_t *_buffer, size_t _sz) -> ssize_t {
        return FeedUploadTask(_buffer, _sz);
    };
    
    auto delegate = [[VFSNetDropboxFileUploadDelegate alloc] initWithStream:stream];
    delegate.handleFinished = [this](int _vfs_error){
        if( m_State == Initiated || m_State == Uploading ) {
            m_State = _vfs_error == VFSError::Ok ?
            Completed :
            Canceled;
            LOCK_GUARD(m_SignalLock) {
                m_Signal.notify_all();
            }
        }
    };
    
    NSMutableURLRequest *request = [[NSMutableURLRequest alloc] initWithURL:api::Upload];
    request.HTTPMethod = @"POST";
    auto &host = *((VFSNetDropboxHost*)Host().get());
    host.FillAuth(request);
    [request setValue:@"application/octet-stream" forHTTPHeaderField:@"Content-Type"];
    InsetHTTPHeaderPathspec(request, RelativePath());
    [request setValue:[NSString stringWithUTF8String:to_string(m_Upload->upload_size).c_str()]
             forHTTPHeaderField:@"Content-Length"];
    
    auto configuration = NSURLSessionConfiguration.defaultSessionConfiguration;
    auto session = [NSURLSession sessionWithConfiguration:configuration
                                                 delegate:delegate
                                            delegateQueue:nil];
    auto task = [session uploadTaskWithStreamedRequest:request];

    m_Upload->request = request;
    m_Upload->delegate = delegate;
    m_Upload->stream = stream;
    m_Upload->task = task;
    m_State = Uploading;

    [task resume];

//{
//    "path": "/Homework/math/Matrices.txt",
//    "mode": "add",
//    "autorename": true,
//    "mute": false
//}

//{
//    "path": "/Homework/math/Matrices.txt",
//    "mode": {
//        ".tag": "update",
//        "update": "a1c10ce0dd78"
//    },
//    "autorename": false,
//    "mute": false
//}

    return VFSError::Ok;
}

int VFSNetDropboxFile::StartBigUpload()
{
    assert( m_Upload != nullptr );
    assert( m_Upload->upload_size > m_ChunkSize );
    assert( m_Upload->request == nil );
    assert( m_Upload->delegate == nil );
    assert( m_Upload->stream == nil );
    assert( m_Upload->task == nil );

    auto stream = [[VFSNetDropboxFileUploadStream alloc] init];
    stream.hasDataToFeed = [this]() -> bool {
        return HasDataToFeedUploadTask();
    };
    stream.feedData = [this](uint8_t *_buffer, size_t _sz) -> ssize_t {
        return FeedUploadTask(_buffer, _sz);
    };
    
    auto delegate = [[VFSNetDropboxFileUploadDelegate alloc] initWithStream:stream];
    delegate.handleFinished = [this](int _vfs_error){
        if( /*m_State == Initiated ||*/ m_State == Uploading )
            if( _vfs_error != VFSError::Ok ) {
                m_State = Canceled;
                LOCK_GUARD(m_SignalLock) {
                    m_Signal.notify_all();
                }
            }
    };
    
    NSMutableURLRequest *request = [[NSMutableURLRequest alloc]initWithURL:api::UploadSessionStart];
    request.HTTPMethod = @"POST";
    auto &host = *((VFSNetDropboxHost*)Host().get());
    host.FillAuth(request);
    [request setValue:@"application/octet-stream" forHTTPHeaderField:@"Content-Type"];
    [request setValue:@"{ }" forHTTPHeaderField:@"Dropbox-API-Arg"];
    [request setValue:[NSString stringWithUTF8String:to_string(m_ChunkSize).c_str()]
             forHTTPHeaderField:@"Content-Length"];
    
    auto configuration = NSURLSessionConfiguration.defaultSessionConfiguration;
    auto session = [NSURLSession sessionWithConfiguration:configuration
                                                 delegate:delegate
                                            delegateQueue:nil];
    auto task = [session uploadTaskWithStreamedRequest:request];

    m_Upload->request = request;
    m_Upload->delegate = delegate;
    m_Upload->stream = stream;
    m_Upload->task = task;
    m_Upload->partitioned = true;
    m_State = Uploading;
    
    [task resume];
    
    return VFSError::Ok;
}

void VFSNetDropboxFile::StartBigFinish()
{
    assert( m_Upload != nullptr );
    assert( m_FilePos >= m_ChunkSize );
    assert( m_Upload->upload_size > m_ChunkSize );
    assert( m_Upload->request != nil );
    assert( m_Upload->delegate != nil );
    assert( m_Upload->stream != nil );
    assert( m_Upload->task != nil );
    assert( !m_Upload->session_id.empty() );

    m_Upload->part_no++;

    auto stream = [[VFSNetDropboxFileUploadStream alloc] init];
    stream.hasDataToFeed = [this]() -> bool {
        return HasDataToFeedUploadTask();
    };
    stream.feedData = [this](uint8_t *_buffer, size_t _sz) -> ssize_t {
        return FeedUploadTask(_buffer, _sz);
    };
    
    auto delegate = [[VFSNetDropboxFileUploadDelegate alloc] initWithStream:stream];
    delegate.handleFinished = [this](int _vfs_error){
        if( m_State == Uploading ) {
            m_State = _vfs_error == VFSError::Ok ? Completed : Canceled;
            LOCK_GUARD(m_SignalLock) {
                m_Signal.notify_all();
            }
        }
    };
    
    NSMutableURLRequest *request = [[NSMutableURLRequest alloc]initWithURL:api::UploadSessionFinish];
    request.HTTPMethod = @"POST";
    auto &host = *((VFSNetDropboxHost*)Host().get());
    host.FillAuth(request);
    
    
    
    const string header =
        "{\"cursor\": {"s +
            "\"session_id\": \"" + m_Upload->session_id + "\", " +
            "\"offset\": " + to_string(m_FilePos) +
        "}, " +
        "\"commit\": {" +
            "\"path\": \"" + EscapeStringForJSONInHTTPHeader(RelativePath()) + "\""
        "}}";
    
    cout << header << endl;
    [request setValue:[NSString stringWithUTF8String:header.c_str()]
             forHTTPHeaderField:@"Dropbox-API-Arg"];
    
//void InsetHTTPHeaderPathspec(NSMutableURLRequest *_request, const string &_path)
//{
//    const string path_spec = "{ \"path\": \"" + EscapeStringForJSONInHTTPHeader(_path) + "\" }";
//    [_request setValue:[NSString stringWithUTF8String:path_spec.c_str()]
//        forHTTPHeaderField:@"Dropbox-API-Arg"];
//}
    
    
    
//    {
//    "cursor": {
//        "session_id": "1234faaf0678bcde",
//        "offset": 0
//    },
//    "commit": {
//        "path": "/Homework/math/Matrices.txt",
//        "mode": "add",
//        "autorename": true,
//        "mute": false
//    }
//}
    

    [request setValue:@"application/octet-stream" forHTTPHeaderField:@"Content-Type"];
//    const long content_length = m_Upload->upload_size - m_Upload->part_no*g_ChunkSize;
    const long content_length = m_Upload->upload_size - m_FilePos;
    cout << "content_length: " << content_length << endl;
    [request setValue:[NSString stringWithUTF8String:to_string(content_length).c_str()]
             forHTTPHeaderField:@"Content-Length"];
    
    auto configuration = NSURLSessionConfiguration.defaultSessionConfiguration;
    auto session = [NSURLSession sessionWithConfiguration:configuration
                                                 delegate:delegate
                                            delegateQueue:nil];
    auto task = [session uploadTaskWithStreamedRequest:request];

    m_Upload->request = request;
    m_Upload->delegate = delegate;
    m_Upload->stream = stream;
    m_Upload->task = task;
//    m_Upload->partitioned = true;
//    m_State = Uploading;
    
    [task resume];
}

int VFSNetDropboxFile::SetUploadSize(size_t _size)
{
    if( !m_Upload ||
        m_State != Initiated )
        return VFSError::InvalidCall;
    if( m_Upload->upload_size >= 0 )
        return VFSError::InvalidCall;
    
    m_Upload->upload_size = _size;
    
    if( _size <= m_ChunkSize )
        return StartSmallUpload();
    else
        return StartBigUpload();
}

ssize_t VFSNetDropboxFile::Write(const void *_buf, size_t _size)
{
    if( !m_Upload )
        return VFSError::InvalidCall;
    if( m_State != Uploading )
        return VFSError::InvalidCall;
    if( m_Upload->upload_size < 0 )
        return VFSError::InvalidCall;
    if( m_FilePos + _size > m_Upload->upload_size )
        return VFSError::InvalidCall;
    
    if( !m_Upload->partitioned ) {
    
        LOCK_GUARD(m_DataLock) {
            m_Upload->fifo.insert(end(m_Upload->fifo),
                                  (const uint8_t*)_buf,
                                  (const uint8_t*)_buf + _size);
            cout << "received " << _size << " bytes from caller" << endl;
            [m_Upload->stream notifyAboutNewData];
        }
        
        // need to wait until either upload task ate all provided data, or any network error occured
//        ssize_t eaten = _size - m_Upload->fifo.size();
        ssize_t eaten = 0;
        while( eaten < _size && m_State != Canceled ) {
           {unique_lock<mutex> lk(m_SignalLock);
            m_Signal.wait(lk);}
            
            lock_guard<mutex> lock{m_DataLock};
            eaten = _size - m_Upload->fifo.size();
        }
        m_FilePos += eaten;
        
        // TODO: process an error if any
        if( m_FilePos == m_Upload->upload_size ) {
            [m_Upload->stream notifyAboutDataEnd];
        }
        
        LOCK_GUARD(m_DataLock) {
            // at this moment FIFO must be either emptied via normal execution, or an error has occured.
            // in that case - be sure that there're no remains of this data block.
            m_Upload->fifo.clear();
        }
        
        return eaten;
    }
    else {
        // figure out amount of information we can consume this call
        size_t this_chunk_left = m_ChunkSize - m_Upload->fifo_offset;
        _size = min(_size, this_chunk_left);
        
         LOCK_GUARD(m_DataLock) {
            m_Upload->fifo.insert(end(m_Upload->fifo),
                                  (const uint8_t*)_buf,
                                  (const uint8_t*)_buf + _size);
            cout << "received " << _size << " bytes from caller" << endl;
            [m_Upload->stream notifyAboutNewData];
        }

        // need to wait until either upload task ate all provided data, or any network error occured
        ssize_t eaten = 0;
        while( eaten < _size && m_State != Canceled ) {
           {unique_lock<mutex> lk(m_SignalLock);
            m_Signal.wait(lk);}
            
            lock_guard<mutex> lock{m_DataLock};
            eaten = _size - m_Upload->fifo.size();
        }
        m_FilePos += eaten;
        
        if( m_Upload->fifo_offset == m_ChunkSize ) {
            // get session id
            // switch upload session
            if( m_Upload->part_no == 0 ) {
                [m_Upload->stream notifyAboutDataEnd];
                m_Upload->stream.feedData = nullptr;
                m_Upload->stream.hasDataToFeed = nullptr;
                m_Upload->delegate.handleReceivedData = [this](NSData *_data){
                    if( auto doc = ParseJSON(_data) )
                        if( auto session_id = GetString(*doc, "session_id") ) {
                            m_Upload->session_id = session_id;
                            lock_guard<mutex> lock{m_SignalLock};
                            m_Signal.notify_all();
                            return;
                        }
                    m_State = State::Canceled;
                    lock_guard<mutex> lock{m_SignalLock};
                    m_Signal.notify_all();
                };
                
                {
                    unique_lock<mutex> lk(m_SignalLock);
                    m_Signal.wait(lk, [this]{
                        if( m_State != Uploading )
                            return true;
                        lock_guard<mutex> lock{m_DataLock};
                        return !m_Upload->session_id.empty();
                    });
                }
                cout << "got session_id: " << m_Upload->session_id << endl;
                m_Upload->delegate.handleReceivedData = nullptr;
                m_Upload->delegate.handleFinished = nullptr;
                
                StartBigFinish();
                
            }
            else {
                [m_Upload->stream notifyAboutDataEnd];
                m_Upload->stream.feedData = nullptr;
                m_Upload->stream.hasDataToFeed = nullptr;
            }
            m_Upload->fifo_offset = 0;
        }

        if( m_FilePos == m_Upload->upload_size ) {
            [m_Upload->stream notifyAboutDataEnd];
        }
        
    
    
        return eaten;
    }
}

ssize_t VFSNetDropboxFile::FeedUploadTask( uint8_t *_buffer, size_t _sz )
{
    if( _sz == 0 )
        return 0;
    
    ssize_t sz = 0;
    
    LOCK_GUARD(m_DataLock) {
        sz = min( _sz, m_Upload->fifo.size() );
        copy_n( begin(m_Upload->fifo), sz, _buffer );
        m_Upload->fifo.erase( begin(m_Upload->fifo), begin(m_Upload->fifo) + sz );
        m_Upload->fifo_offset += sz;
        cout << "fed " << sz << " bytes into stream" << endl;
    }
    
    if( sz != 0 ) {    
        LOCK_GUARD(m_SignalLock) {
            m_Signal.notify_all();
        }
    }
    return sz;
}

bool VFSNetDropboxFile::HasDataToFeedUploadTask()
{
    bool has_data = false;
    LOCK_GUARD(m_DataLock) {
        has_data = !m_Upload->fifo.empty();
    }
    cout << "has data for stream: " << has_data << endl;
    return has_data;
}

void VFSNetDropboxFile::SetChunkSize( size_t _size )
{
    if( _size >= 1 * 1000 * 1000 && _size <= 150 * 1000 * 1000 )
        m_ChunkSize = _size;
}
