#ifndef _SOS_CACHE_H_
#define _SOS_CACHE_H_

// Comment this out to re-enable the cache
//#define SOS_NO_CACHE

#ifdef SOS_NO_CACHE
	#define DEFAULT_MEMORY L4_UncachedMemory
	#define CACHE_FLUSH_ALL() ((void) L4_CacheFlushAll)
	#define CACHE_FLUSH_RANGE(sid, start, end) (1)
	#define CACHE_FLUSH_RANGE_INVALIDATE(sid, start, end) (1)
#else
	#define DEFAULT_MEMORY L4_DefaultMemory
	#define CACHE_FLUSH_ALL() L4_CacheFlushAll()
	#define CACHE_FLUSH_RANGE(sid, start, end) L4_CacheFlushRange((sid), (start), (end))
	#define CACHE_FLUSH_RANGE_INVALIDATE(sid, start, end) L4_CacheFlushRangeInvalidate((sid), (start), (end))
#endif

#endif // sos/cache.h
