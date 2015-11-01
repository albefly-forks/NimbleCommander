#include <sys/xattr.h>
#include "xattr.h"
#include "../VFSFile.h"

class VFSXAttrFile final: public VFSFile
{
public:
    VFSXAttrFile( const string &_xattr_path, const shared_ptr<VFSXAttrHost> &_parent, int _fd );
    virtual int Open(int _open_flags, VFSCancelChecker _cancel_checker = nullptr) override;
    virtual int  Close() override;
    virtual bool IsOpened() const override;
    virtual ReadParadigm  GetReadParadigm() const override;
    virtual WriteParadigm GetWriteParadigm() const override;
    virtual ssize_t Pos() const override;
    virtual off_t Seek(off_t _off, int _basis) override;
    virtual ssize_t Size() const override;
    virtual bool Eof() const override;
    virtual ssize_t Read(void *_buf, size_t _size) override;
    virtual ssize_t ReadAt(off_t _pos, void *_buf, size_t _size) override;
    virtual ssize_t Write(const void *_buf, size_t _size) override;
    virtual int SetUploadSize(size_t _size) override;
    
private:
    const char *XAttrName() const noexcept;
    bool IsOpenedForReading() const noexcept;
    bool IsOpenedForWriting() const noexcept;
    
    const int               m_FD; // non-owning
    int                     m_OpenFlags = 0;
    unique_ptr<uint8_t[]>   m_FileBuf;
    ssize_t                 m_Position = 0;
    ssize_t                 m_Size = 0;
    ssize_t                 m_UploadSize = -1;
};

// XATTR_MAXNAMELEN

//The maximum supported size of extended attribute can be found out using pathconf(2) with
//_PC_XATTR_SIZE_BITS option.

//// get current file descriptor's open flags
//{


///* Options for pathname based xattr calls */
//#define XATTR_NOFOLLOW   0x0001     /* Don't follow symbolic links */
//
///* Options for setxattr calls */
//#define XATTR_CREATE     0x0002     /* set the value, fail if attr already exists */
//#define XATTR_REPLACE    0x0004     /* set the value, fail if attr does not exist */
//
///* Set this to bypass authorization checking (eg. if doing auth-related work) */
//#define XATTR_NOSECURITY 0x0008
//
///* Set this to bypass the default extended attribute file (dot-underscore file) */
//#define XATTR_NODEFAULT  0x0010
//
///* option for f/getxattr() and f/listxattr() to expose the HFS Compression extended attributes */
//#define XATTR_SHOWCOMPRESSION 0x0020
//
//#define	XATTR_MAXNAMELEN   127
//
///* See the ATTR_CMN_FNDRINFO section of getattrlist(2) for details on FinderInfo */
//#define	XATTR_FINDERINFO_NAME	  "com.apple.FinderInfo"
//
//#define	XATTR_RESOURCEFORK_NAME	  "com.apple.ResourceFork"
//ssize_t getxattr(const char *path, const char *name, void *value, size_t size, u_int32_t position, int options);
//ssize_t fgetxattr(int fd, const char *name, void *value, size_t size, u_int32_t position, int options);
//int setxattr(const char *path, const char *name, const void *value, size_t size, u_int32_t position, int options);
//int fsetxattr(int fd, const char *name, const void *value, size_t size, u_int32_t position, int options);
//int removexattr(const char *path, const char *name, int options);
//int fremovexattr(int fd, const char *name, int options);
//ssize_t listxattr(const char *path, char *namebuff, size_t size, int options);
//ssize_t flistxattr(int fd, char *namebuff, size_t size, int options);

//    if( !_path || _path[0] != '/' )
static bool is_absolute_path( const char *_s ) noexcept
{
    return _s != nullptr && _s[0] == '/';
}

static bool TurnOffBlockingMode( int _fd ) noexcept
{
    int fcntl_ret = fcntl(_fd, F_GETFL);
    if( fcntl_ret < 0 )
        return false;
    
    fcntl_ret = fcntl(_fd, F_SETFL, fcntl_ret & ~O_NONBLOCK);
    if( fcntl_ret < 0 )
        return false;
    
    return true;
}

static int EnumerateAttrs( int _fd, vector<pair<string, unsigned>> &_attrs )
{
    const auto buf_sz = 65536;
    char buf[buf_sz];
    auto used_size = flistxattr(_fd, buf, buf_sz, 0);
    if( used_size < 0) // need to process ERANGE later. if somebody wanna mess with 65536/XATTR_MAXNAMELEN=512 xattrs per entry...
        return VFSError::FromErrno();

    for( auto s = buf, e = buf + used_size; s < e; s += strlen(s) + 1 ) { // iterate thru xattr names..
        auto xattr_size = fgetxattr(_fd, s, nullptr, 0, 0, 0);
        if( xattr_size >= 0 )
            _attrs.emplace_back(s, xattr_size);
    }
    
    return 0;
}

const char *VFSXAttrHost::Tag = "xattr";
static const mode_t g_RegMode = S_IRUSR | S_IWUSR | S_IFREG;
static const mode_t g_RootMode = S_IRUSR | S_IXUSR | S_IFDIR;

class VFSXAttrHostConfiguration
{
public:
    VFSXAttrHostConfiguration(const string &_path):
        path(_path),
        verbose_junction("[xattr]:"s + _path)
    {
    }
    
    const string path;
    const string verbose_junction;
    
    const char *Tag() const
    {
        return VFSXAttrHost::Tag;
    }
    
    const char *Junction() const
    {
        return path.c_str();
    }
    
    const char *VerboseJunction() const
    {
        return verbose_junction.c_str();
    }
    
    bool operator==(const VFSXAttrHostConfiguration&_rhs) const
    {
        return path == _rhs.path;
    }
};

VFSXAttrHost::VFSXAttrHost( const string &_file_path, const VFSHostPtr& _host ):
    VFSXAttrHost( _host,
                 VFSConfiguration( VFSXAttrHostConfiguration(_file_path) )
                 )
{
}

VFSXAttrHost::VFSXAttrHost(const VFSHostPtr &_parent, const VFSConfiguration &_config):
    VFSHost( _config.Get<VFSXAttrHostConfiguration>().path.c_str(), _parent, Tag ),
    m_Configuration(_config)
{
    auto path = JunctionPath();
    if( !_parent->IsNativeFS() )
        throw VFSErrorException(VFSError::InvalidCall);
    
    int fd =          open( path, O_RDONLY|O_NONBLOCK|O_EXLOCK);
    if( fd < 0 ) fd = open( path, O_RDONLY|O_NONBLOCK|O_SHLOCK);
    if( fd < 0 ) fd = open( path, O_RDONLY|O_NONBLOCK);
    if( fd < 0 )
        throw VFSErrorException( VFSError::FromErrno(EIO) );
    
    if( !TurnOffBlockingMode(fd) ) {
        close(fd);
        throw VFSErrorException( VFSError::FromErrno(EIO) );
    }
    
    if( fstat(fd, &m_Stat) != 0) {
        close(fd);
        throw VFSErrorException( VFSError::FromErrno(EIO) );
    }
    
    int ret = EnumerateAttrs( fd, m_Attrs );
    if( ret != 0) {
        close(fd);
        throw VFSErrorException(ret);
    }
    
    m_FD = fd;
}

VFSXAttrHost::~VFSXAttrHost()
{
    close(m_FD);
}

VFSConfiguration VFSXAttrHost::Configuration() const
{
    return m_Configuration;
}

VFSMeta VFSXAttrHost::Meta()
{
    VFSMeta m;
    m.Tag = Tag;
    m.SpawnWithConfig = [](const VFSHostPtr &_parent, const VFSConfiguration& _config) {
        return make_shared<VFSXAttrHost>(_parent, _config);
    };
    return m;
}

bool VFSXAttrHost::IsWriteable() const
{
    return true;
}

int VFSXAttrHost::Fetch()
{
    vector<pair<string, unsigned>> info;
    int ret = EnumerateAttrs(m_FD, info);
    if( ret != 0)
        return ret;
    
    lock_guard<spinlock> lock(m_AttrsLock);
    m_Attrs = move(info);
    return VFSError::Ok;
}

int VFSXAttrHost::FetchFlexibleListing(const char *_path,
                                       shared_ptr<VFSFlexibleListing> &_target,
                                       int _flags,
                                       VFSCancelChecker _cancel_checker)
{
    if( !_path || _path != string_view("/") )
        return VFSError::InvalidCall;
    
    // set up or listing structure
    VFSFlexibleListingInput listing_source;
    listing_source.hosts[0] = shared_from_this();
    listing_source.directories[0] = "/";
    listing_source.atimes.reset( variable_container<>::type::common );
    listing_source.mtimes.reset( variable_container<>::type::common );
    listing_source.ctimes.reset( variable_container<>::type::common );
    listing_source.btimes.reset( variable_container<>::type::common );
    listing_source.sizes.reset( variable_container<>::type::dense );
    listing_source.atimes[0] = m_Stat.st_atime;
    listing_source.ctimes[0] = m_Stat.st_ctime;
    listing_source.btimes[0] = m_Stat.st_birthtime;
    listing_source.mtimes[0] = m_Stat.st_mtime;
    
    {
        lock_guard<spinlock> lock(m_AttrsLock);
        
        if( !(_flags & VFSFlags::F_NoDotDot) ) {
            listing_source.filenames.emplace_back( ".." );
            listing_source.unix_types.emplace_back( DT_DIR );
            listing_source.unix_modes.emplace_back( g_RootMode );
            listing_source.sizes.insert( 0, 0 );
        }
        
        for( const auto &i: m_Attrs ) {
            listing_source.filenames.emplace_back( i.first );
            listing_source.unix_types.emplace_back( DT_REG );
            listing_source.unix_modes.emplace_back( g_RegMode );
            listing_source.sizes.insert( listing_source.filenames.size()-1, i.second );
        }
    }
    
    _target = VFSFlexibleListing::Build(move(listing_source));
    return VFSError::Ok;
}

int VFSXAttrHost::Stat(const char *_path, VFSStat &_st, int _flags, VFSCancelChecker _cancel_checker)
{
    if( !is_absolute_path(_path) )
        return VFSError::NotFound;

    memset(&_st, sizeof(_st), 0);
    _st.meaning.size = true;
    _st.meaning.mode = true;
    _st.meaning.atime = true;
    _st.meaning.btime = true;
    _st.meaning.ctime = true;
    _st.meaning.mtime = true;
    _st.atime = m_Stat.st_atimespec;
    _st.mtime = m_Stat.st_mtimespec;
    _st.btime = m_Stat.st_birthtimespec;
    _st.ctime = m_Stat.st_ctimespec;
    
    auto path = string_view(_path);
    if( path == "/" ) {
        _st.mode = g_RootMode;
        _st.size = 0;
        return VFSError::Ok;
    }
    else if( path.length() > 1 ) {
        path.remove_prefix(1);    
        for( auto &i: m_Attrs )
            if( path == i.first ) {
                _st.mode = g_RegMode;
                _st.size = i.second;
                return 0;
            }
    }
    
    return VFSError::FromErrno(ENOENT);
}

int VFSXAttrHost::CreateFile(const char* _path,
                             shared_ptr<VFSFile> &_target,
                             VFSCancelChecker _cancel_checker)
{
    auto file = make_shared<VFSXAttrFile>(_path, static_pointer_cast<VFSXAttrHost>(shared_from_this()), m_FD);
    if(_cancel_checker && _cancel_checker())
        return VFSError::Cancelled;
    _target = file;
    return VFSError::Ok;
}

int VFSXAttrHost::Unlink(const char *_path, VFSCancelChecker _cancel_checker)
{
    if( !_path || _path[0] != '/' )
        return VFSError::FromErrno(ENOENT);
    
    if( fremovexattr(m_FD, _path+1, 0) == -1 )
        return VFSError::FromErrno();
    
    ReportChange();
    
    return VFSError::Ok;
}

int VFSXAttrHost::Rename(const char *_old_path, const char *_new_path, VFSCancelChecker _cancel_checker)
{
    if( !_old_path || _old_path[0] != '/' ||
        !_new_path || _new_path[0] != '/' )
        return VFSError::FromErrno(ENOENT);
    
    const auto old_path = _old_path+1;
    const auto new_path = _new_path+1;
    
    const auto xattr_size = fgetxattr(m_FD, old_path, nullptr, 0, 0, 0);
    if( xattr_size < 0 )
        return VFSError::FromErrno();
    
    const auto buf = make_unique<uint8_t[]>(xattr_size);
    if( fgetxattr(m_FD, old_path, buf.get(), xattr_size, 0, 0) < 0 )
        return VFSError::FromErrno();
    
    if( fsetxattr(m_FD, new_path, buf.get(), xattr_size, 0, 0) < 0 )
        return VFSError::FromErrno();
    
    if( fremovexattr(m_FD, old_path, 0) < 0 )
        return VFSError::FromErrno();
    
    ReportChange();
    
    return VFSError::Ok;
}

bool VFSXAttrHost::ShouldProduceThumbnails() const
{
    return false;
}

void VFSXAttrHost::ReportChange()
{
    Fetch();

    // observers
}

// hardly needs own version of this, since xattr will happily work with abra:cadabra filenames
//bool VFSHost::ValidateFilename(const char *_filename) const


VFSXAttrFile::VFSXAttrFile( const string &_xattr_path, const shared_ptr<VFSXAttrHost> &_parent, int _fd ):
    VFSFile(_xattr_path.c_str(), _parent),
    m_FD(_fd)
{
}

int VFSXAttrFile::Open(int _open_flags, VFSCancelChecker _cancel_checker)
{
    if( IsOpened() )
        return VFSError::InvalidCall;
    
    Close();

    const auto path = XAttrName();
    if( !path )
        return VFSError::FromErrno(ENOENT);
    
    if( _open_flags & VFSFlags::OF_Write ) {
        if( _open_flags & VFSFlags::OF_Append )
            return VFSError::NotSupported;
        // TODO: OF_NoExist
        
        m_OpenFlags = _open_flags;
    }
    else if( _open_flags & VFSFlags::OF_Read ) {
        auto xattr_size = fgetxattr(m_FD, path, nullptr, 0, 0, 0);
        if( xattr_size < 0 )
            return VFSError::FromErrno(ENOENT);
    
        m_FileBuf = make_unique<uint8_t[]>(xattr_size);
        if( fgetxattr(m_FD, path, m_FileBuf.get(), xattr_size, 0, 0) < 0 )
            return VFSError::FromErrno();
        
        m_Size = xattr_size;
        m_OpenFlags = _open_flags;
    }
    
    return VFSError::Ok;
}

int VFSXAttrFile::Close()
{
    m_Size = 0;
    m_FileBuf.reset();
    m_OpenFlags = 0;
    m_Position = 0;
    m_UploadSize = -1;
    return 0;
}

bool VFSXAttrFile::IsOpened() const
{
    return m_OpenFlags != 0;
}

VFSFile::ReadParadigm VFSXAttrFile::GetReadParadigm() const
{
    return VFSFile::ReadParadigm::Random;
}

VFSFile::WriteParadigm VFSXAttrFile::GetWriteParadigm() const
{
    return VFSFile::WriteParadigm::Upload;
}

ssize_t VFSXAttrFile::Pos() const
{
    return m_Position;
}

ssize_t VFSXAttrFile::Size() const
{
    return m_Size;
}

bool VFSXAttrFile::Eof() const
{
    return m_Position >= m_Size;
}

off_t VFSXAttrFile::Seek(off_t _off, int _basis)
{
    if(!IsOpened())
        return VFSError::InvalidCall;
    
    if( !IsOpenedForReading() )
        return VFSError::InvalidCall;
        
    off_t req_pos = 0;
    if(_basis == VFSFile::Seek_Set)
        req_pos = _off;
    else if(_basis == VFSFile::Seek_End)
        req_pos = m_Size + _off;
    else if(_basis == VFSFile::Seek_Cur)
        req_pos = m_Position + _off;
    else
        return VFSError::InvalidCall;
    
    if(req_pos < 0)
        return VFSError::InvalidCall;
    if(req_pos > m_Size)
        req_pos = m_Size;
    m_Position = req_pos;
    
    return m_Position;
}

ssize_t VFSXAttrFile::Read(void *_buf, size_t _size)
{
    if( !IsOpened() || !IsOpenedForReading() )
        return SetLastError(VFSError::InvalidCall);

    if( m_Position == m_Size )
        return 0;
    
    ssize_t to_read = min( m_Size - m_Position, ssize_t(_size) );
    if( to_read <= 0 )
        return 0;
    
    memcpy( _buf, m_FileBuf.get() + m_Position, to_read );
    m_Position += to_read;
    
    return to_read;
}

ssize_t VFSXAttrFile::ReadAt(off_t _pos, void *_buf, size_t _size)
{
    if( !IsOpened() || !IsOpenedForReading() )
        return SetLastError(VFSError::InvalidCall);
    
    if( _pos < 0 || _pos > m_Size )
        return SetLastError( VFSError::FromErrno(EINVAL) );
    
    auto sz = min( m_Size - _pos, off_t(_size) );
    memcpy(_buf, m_FileBuf.get() + _pos, sz );
    return sz;
}

bool VFSXAttrFile::IsOpenedForReading() const noexcept
{
    return m_OpenFlags & VFSFlags::OF_Read;
}

bool VFSXAttrFile::IsOpenedForWriting() const noexcept
{
    return m_OpenFlags & VFSFlags::OF_Write;
}

int VFSXAttrFile::SetUploadSize(size_t _size)
{
    if( !IsOpenedForWriting() )
        return VFSError::FromErrno( EINVAL );
    
    if( m_UploadSize >= 0 )
        return VFSError::FromErrno( EINVAL ); // already reported before
    
    // TODO: check max xattr size and reject huge ones

    m_UploadSize = _size;
    m_FileBuf = make_unique<uint8_t[]>(_size);
    
    if( _size == 0 ) {
        // for zero-size uploading - do it right here        
        char buf[1];
        if( fsetxattr(m_FD, XAttrName(), buf, 0, 0, 0) != 0 )
            return VFSError::FromErrno();
        
        dynamic_pointer_cast<VFSXAttrHost>(Host())->ReportChange();        
    }
    
    return 0;
}

ssize_t VFSXAttrFile::Write(const void *_buf, size_t _size)
{
    if( !IsOpenedForWriting() ||
        !m_FileBuf )
        return VFSError::FromErrno(EIO);
    
    if( m_Position < m_UploadSize ) {
        ssize_t to_write = min( m_UploadSize - m_Position, (ssize_t)_size );
        memcpy( m_FileBuf.get() + m_Position, _buf, to_write );
        m_Position += to_write;
        
        if( m_Position == m_UploadSize ) {
            // time to flush

            if( fsetxattr(m_FD, XAttrName(), m_FileBuf.get(), m_UploadSize, 0, 0) != 0 )
                return VFSError::FromErrno();
            
            dynamic_pointer_cast<VFSXAttrHost>(Host())->ReportChange();
        }
        return to_write;
    }
    return 0;
}

const char *VFSXAttrFile::XAttrName() const noexcept
{
    const char *path = RelativePath();
    if( path[0] != '/' )
        return nullptr;
    return path + 1;
}