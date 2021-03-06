/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
 * Copyright 2020 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include <osgEarth/Threading>
#include <osgDB/Options>
#include <osg/OperationThread>
#include "Utils"
#include "Metrics"

#ifdef _WIN32
  #ifndef OSGEARTH_PROFILING
    // because Tracy already does this in its header file..
    extern "C" unsigned long __stdcall GetCurrentThreadId();
  #endif
#elif defined(__APPLE__) || defined(__LINUX__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__ANDROID__)
#   include <unistd.h>
#   include <sys/syscall.h>
#else
#   include <pthread.h>
#endif

using namespace osgEarth::Threading;
using namespace osgEarth::Util;

//...................................................................

#ifdef OSGEARTH_PROFILING
#define MUTEX_TYPE tracy::Lockable<std::recursive_mutex>
#else
#define MUTEX_TYPE std::recursive_mutex
#endif

Mutex::Mutex()
{
#ifdef OSGEARTH_PROFILING
    tracy::SourceLocationData* s = new tracy::SourceLocationData();
    s->name = nullptr;
    s->function = "unnamed";
    s->file = __FILE__;
    s->line = __LINE__;
    s->color = 0;
    _handle = new tracy::Lockable<std::mutex>(s);
    _data = s;
#else
    _handle = new std::mutex();
    _data = NULL;
#endif
}

Mutex::Mutex(const std::string& name, const char* file, std::uint32_t line) :
    _name(name)
{
#ifdef OSGEARTH_PROFILING        
    tracy::SourceLocationData* s = new tracy::SourceLocationData();
    s->name = nullptr;
    s->function = _name.c_str();
    s->file = file;
    s->line = line;
    s->color = 0;
    _handle = new tracy::Lockable<std::mutex>(s);
    _data = s;
#else
    _handle = new std::mutex();
    _data = NULL;
#endif
}

Mutex::~Mutex()
{
    delete static_cast<MUTEX_TYPE*>(_handle);
}

void
Mutex::setName(const std::string& name)
{
    _name = name;
#ifdef OSGEARTH_PROFILING
    if (_data)
    {
        tracy::SourceLocationData* s = static_cast<tracy::SourceLocationData*>(_data);
        s->function = _name.c_str();
    }
#endif
}

void
Mutex::lock()
{
    if (_name.empty()) {
        volatile int i=0; // breakpoint for finding unnamed mutexes -GW
    }
    static_cast<MUTEX_TYPE*>(_handle)->lock();
}

void
Mutex::unlock()
{
    static_cast<MUTEX_TYPE*>(_handle)->unlock();
}

bool
Mutex::try_lock()
{
    return static_cast<MUTEX_TYPE*>(_handle)->try_lock();
}

//...................................................................

#ifdef OSGEARTH_PROFILING
#define RECURSIVE_MUTEX_TYPE tracy::Lockable<std::recursive_mutex>
#else
#define RECURSIVE_MUTEX_TYPE std::recursive_mutex
#endif

RecursiveMutex::RecursiveMutex() :
    _enabled(true)
{
#ifdef OSGEARTH_PROFILING
    tracy::SourceLocationData* s = new tracy::SourceLocationData();
    s->name = nullptr;
    s->function = "unnamed recursive";
    s->file = __FILE__;
    s->line = __LINE__;
    s->color = 0;
    _handle = new tracy::Lockable<std::recursive_mutex>(s);
#else
    _handle = new std::recursive_mutex();
#endif
}

RecursiveMutex::RecursiveMutex(const std::string& name, const char* file, std::uint32_t line) :
    _name(name),
    _enabled(true)
{
#ifdef OSGEARTH_PROFILING        
    tracy::SourceLocationData* s = new tracy::SourceLocationData();
    s->name = nullptr;
    s->function = _name.c_str();
    s->file = file;
    s->line = line;
    s->color = 0;
    _handle = new tracy::Lockable<std::recursive_mutex>(s);
#else
    _handle = new std::recursive_mutex();
#endif
}

RecursiveMutex::~RecursiveMutex()
{
    if (_handle)
        delete static_cast<RECURSIVE_MUTEX_TYPE*>(_handle);
}

void
RecursiveMutex::disable()
{
    _enabled = false;
}

void
RecursiveMutex::lock()
{
    if (_enabled)
        static_cast<RECURSIVE_MUTEX_TYPE*>(_handle)->lock();
}

void
RecursiveMutex::unlock()
{
    if (_enabled)
        static_cast<RECURSIVE_MUTEX_TYPE*>(_handle)->unlock();
}

bool
RecursiveMutex::try_lock()
{
    if (_enabled)
        return static_cast<MUTEX_TYPE*>(_handle)->try_lock();
    else
        return true;
}

//...................................................................

unsigned osgEarth::Threading::getCurrentThreadId()
{  
#ifdef _WIN32
  return (unsigned)::GetCurrentThreadId();
#elif __APPLE__
  return ::syscall(SYS_thread_selfid);
#elif __ANDROID__
  return gettid();
#elif __LINUX__
  return (unsigned)::syscall(SYS_gettid);
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  long  tid;
  syscall(SYS_thr_self, &tid);
  return (unsigned)tid;
#else
  /* :XXX: this truncates to 32 bits, but better than nothing */
  return (unsigned)pthread_self();
#endif
}

//...................................................................

Event::Event() :
_set(false)
{
    //nop
}
Event::Event(const std::string& name) :
    _set(false),
    _m(name)
{
    //nop
}

Event::~Event()
{
    _set = false;
    for(int i=0; i<255; ++i)  // workaround buggy broadcast
        _cond.notify_all();
}

void
Event::setName(const std::string& name)
{
    _m.setName(name);
}

bool Event::wait()
{
    std::unique_lock<Mutex> lock(_m);
    if (!_set)
        _cond.wait(lock);
    return true;
}

bool Event::wait(unsigned timeout_ms)
{
    std::unique_lock<Mutex> lock(_m);
    if (!_set)
    {
        std::cv_status result = _cond.wait_for(lock, std::chrono::milliseconds(timeout_ms));
        return result == std::cv_status::no_timeout ? true : false;
    }
    return true;
}

bool Event::waitAndReset()
{
    std::unique_lock<Mutex> lock(_m);
    if (!_set)
        _cond.wait(lock);
    _set = false;
    return true;
}

void Event::set()
{
    std::unique_lock<Mutex> lock(_m);
    if (!_set) {
        _set = true;
        _cond.notify_all();
    }
}

void Event::reset()
{
    std::unique_lock<Mutex> lock(_m);
    _set = false;
}

//...................................................................

ReadWriteMutex::ReadWriteMutex() :
    _readers(0), _writers(0)
{
    //NOP
}

ReadWriteMutex::ReadWriteMutex(const std::string& name) :
    _readers(0), _writers(0), _m(name)
{
    //NOP
}

void ReadWriteMutex::read_lock()
{
    std::unique_lock<Mutex> lock(_m);
    while (_writers > 0)
        _unlocked.wait(lock);
    ++_readers;
}

void ReadWriteMutex::read_unlock()
{
    std::unique_lock<Mutex> lock(_m);
    --_readers;
    if (_readers == 0)
        _unlocked.notify_all();
}

void ReadWriteMutex::write_lock()
{
    std::unique_lock<Mutex> lock(_m);
    while (_writers > 0 || _readers > 0)
        _unlocked.wait(lock);
    ++_writers;
}

void ReadWriteMutex::write_unlock()
{
    std::unique_lock<Mutex> lock(_m);
    _writers = 0;
    _unlocked.notify_all();
}

void ReadWriteMutex::setName(const std::string& name)
{
    _m.setName(name);
}

#undef LC
#define LC "[ThreadPool] "

ThreadPool::ThreadPool(unsigned int numThreads) :
    _numThreads(numThreads),
    _done(false),
    _queueMutex("ThreadPool")
{
    startThreads();
}

ThreadPool::~ThreadPool()
{
    stopThreads();
}

void ThreadPool::run(osg::Operation* op)
{
    if (op)
    {
        Threading::ScopedMutexLock lock(_queueMutex);
        _queue.push(op);
        _block.notify_all();
    }
}

unsigned ThreadPool::getNumOperationsInQueue() const
{
    return _queue.size();
}

void ThreadPool::startThreads()
{
    _done = false;

    for(unsigned i=0; i<_numThreads; ++i)
    {
        _threads.push_back(std::thread( [this]
        {
            OE_DEBUG << LC << "Thread " << std::this_thread::get_id() << " started." << std::endl;
            while(!_done)
            {
                osg::ref_ptr<osg::Operation> op;
                {
                    std::unique_lock<Mutex> lock(_queueMutex);

                    _block.wait(lock, [this] {
                        return !_queue.empty() || _done;
                    });

                    if (!_queue.empty() && !_done)
                    {
                        op = _queue.front();
                        _queue.pop();
                    }
                }

                if (op.valid())
                {
                    // run the op:
                    (*op.get())(nullptr);

                    // if it's a keeper, requeue it
                    if (op->getKeep())
                    {
                        Threading::ScopedMutexLock lock(_queueMutex);
                        _queue.push(op);
                    }
                    op = nullptr;
                }
            }
            OE_DEBUG << LC << "Thread " << std::this_thread::get_id() << " exiting." << std::endl;
        }));
    }
}

void ThreadPool::stopThreads()
{
    _done = true;
    _block.notify_all();

    for(unsigned i=0; i<_numThreads; ++i)
    {
        if (_threads[i].joinable())
        {
            _threads[i].join();
        }
    }

    _threads.clear();
    
    // Clear out the queue
    {
        Threading::ScopedMutexLock lock(_queueMutex);
        _queue.swap(Queue());
    }
}

void
ThreadPool::put(osgDB::Options* options)
{
    if (options)
    {
        OptionsData<ThreadPool>::set(options, "osgEarth::ThreadPool", this);
    }
}

osg::ref_ptr<ThreadPool>
ThreadPool::get(const osgDB::Options* options)
{
    if (!options) return NULL;
    return OptionsData<ThreadPool>::get(options, "osgEarth::ThreadPool");
}

