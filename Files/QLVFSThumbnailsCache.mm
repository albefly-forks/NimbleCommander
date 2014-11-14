//
//  QLVFSThumbnailsCache.mm
//  Files
//
//  Created by Michael G. Kazakov on 14/11/14.
//  Copyright (c) 2014 Michael G. Kazakov. All rights reserved.
//

#include "QLVFSThumbnailsCache.h"

static const nanoseconds g_PurgeDelay = 1min;

QLVFSThumbnailsCache &QLVFSThumbnailsCache::Instance() noexcept
{
    static QLVFSThumbnailsCache *inst = new QLVFSThumbnailsCache; // never delete
    return *inst;
}

NSImageRep *QLVFSThumbnailsCache::Get(const string& _path, const VFSHostPtr &_host)
{
    lock_guard<mutex> lock(m_Lock);
    
    auto db = find_if(begin(m_Caches), end(m_Caches), [&](auto &_) { return _.host_raw == _host.get(); });
    if(db == end(m_Caches))
        return nil;
  
    auto img = db->images.find(_path);
    if(img == end(db->images))
        return nil;
    
    return img->second;
}

void QLVFSThumbnailsCache::Put(const string& _path, const VFSHostPtr &_host, NSImageRep *_img)
{
    lock_guard<mutex> lock(m_Lock);
    
    auto db_it = find_if(begin(m_Caches), end(m_Caches), [&](auto &_) { return _.host_raw == _host.get(); });
    Cache *cache;
    if(db_it != end(m_Caches))
        cache = &(*db_it);
    else {
        m_Caches.emplace_front();
        cache = &m_Caches.front();
        cache->host_weak = _host;
        cache->host_raw  = _host.get();
        
        if(!m_PurgeScheduled) {
            m_PurgeScheduled = true;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, g_PurgeDelay.count()),
                           dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
                Purge();
            });
        }
    }
    
    cache->images[_path] = _img;
}

void QLVFSThumbnailsCache::Purge()
{
    assert(m_PurgeScheduled);
    lock_guard<mutex> lock(m_Lock);
    m_Caches.remove_if([](auto &_t) { return _t.host_weak.expired(); });

    if(m_Caches.empty())
        m_PurgeScheduled = false;
    else
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, g_PurgeDelay.count()),
                       dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
            Purge();
        });
}
