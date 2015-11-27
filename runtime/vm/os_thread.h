// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_OS_THREAD_H_
#define VM_OS_THREAD_H_

#include "platform/globals.h"
#include "vm/allocation.h"
#include "vm/globals.h"

// Declare the OS-specific types ahead of defining the generic classes.
#if defined(TARGET_OS_ANDROID)
#include "vm/os_thread_android.h"
#elif defined(TARGET_OS_LINUX)
#include "vm/os_thread_linux.h"
#elif defined(TARGET_OS_MACOS)
#include "vm/os_thread_macos.h"
#elif defined(TARGET_OS_WINDOWS)
#include "vm/os_thread_win.h"
#else
#error Unknown target os.
#endif

namespace dart {

// Forward declarations.
class Log;
class Mutex;
class Thread;
class TimelineEventBlock;

class BaseThread {
 public:
  bool is_os_thread() const { return is_os_thread_; }

 private:
  explicit BaseThread(bool is_os_thread) : is_os_thread_(is_os_thread) {}
  ~BaseThread() {}

  bool is_os_thread_;

  friend class Thread;
  friend class OSThread;

  DISALLOW_IMPLICIT_CONSTRUCTORS(BaseThread);
};


// Low-level operations on OS platform threads.
class OSThread : public BaseThread {
 public:
  OSThread();
  ~OSThread();

  ThreadId id() const {
    ASSERT(id_ != OSThread::kInvalidThreadId);
    return id_;
  }

  ThreadId join_id() const {
    ASSERT(join_id_ != OSThread::kInvalidThreadJoinId);
    return join_id_;
  }

  ThreadId trace_id() const {
    ASSERT(trace_id_ != OSThread::kInvalidThreadJoinId);
    return trace_id_;
  }

  const char* name() const {
    return name_;
  }

  void set_name(const char* name) {
    ASSERT(OSThread::Current() == this);
    ASSERT(name_ == NULL);
    ASSERT(name != NULL);
    name_ = strdup(name);
  }

  Mutex* timeline_block_lock() const {
    return timeline_block_lock_;
  }

  // Only safe to access when holding |timeline_block_lock_|.
  TimelineEventBlock* timeline_block() const {
    return timeline_block_;
  }

  // Only safe to access when holding |timeline_block_lock_|.
  void set_timeline_block(TimelineEventBlock* block) {
    timeline_block_ = block;
  }

  Log* log() const { return log_; }

  uword stack_base() const { return stack_base_; }
  void set_stack_base(uword stack_base) { stack_base_ = stack_base; }

  // Retrieve the stack address bounds for profiler.
  bool GetProfilerStackBounds(uword* lower, uword* upper) const {
    uword stack_upper = stack_base_;
    if (stack_upper == 0) {
      return false;
    }
    uword stack_lower = stack_upper - GetSpecifiedStackSize();
    *lower = stack_lower;
    *upper = stack_upper;
    return true;
  }

  // Used to temporarily disable or enable thread interrupts.
  void DisableThreadInterrupts();
  void EnableThreadInterrupts();
  bool ThreadInterruptsEnabled();

  // The currently executing thread, or NULL if not yet initialized.
  static OSThread* Current() {
    BaseThread* thread = GetCurrentTLS();
    OSThread* os_thread = NULL;
    if (thread != NULL) {
      if (thread->is_os_thread()) {
        os_thread = reinterpret_cast<OSThread*>(thread);
      } else {
        Thread* vm_thread = reinterpret_cast<Thread*>(thread);
        os_thread = GetOSThreadFromThread(vm_thread);
      }
    }
    return os_thread;
  }
  static void SetCurrent(OSThread* current);

  // TODO(5411455): Use flag to override default value and Validate the
  // stack size by querying OS.
  static uword GetSpecifiedStackSize() {
    ASSERT(OSThread::kStackSizeBuffer < OSThread::GetMaxStackSize());
    uword stack_size = OSThread::GetMaxStackSize() - OSThread::kStackSizeBuffer;
    return stack_size;
  }
  static BaseThread* GetCurrentTLS() {
    return reinterpret_cast<BaseThread*>(OSThread::GetThreadLocal(thread_key_));
  }
  static void SetCurrentTLS(uword value) {
    SetThreadLocal(thread_key_, value);
  }

  typedef void (*ThreadStartFunction) (uword parameter);
  typedef void (*ThreadDestructor) (void* parameter);

  // Start a thread running the specified function. Returns 0 if the
  // thread started successfuly and a system specific error code if
  // the thread failed to start.
  static int Start(
      const char* name, ThreadStartFunction function, uword parameter);

  static ThreadLocalKey CreateThreadLocal(ThreadDestructor destructor = NULL);
  static void DeleteThreadLocal(ThreadLocalKey key);
  static uword GetThreadLocal(ThreadLocalKey key) {
    return ThreadInlineImpl::GetThreadLocal(key);
  }
  static ThreadId GetCurrentThreadId();
  static void SetThreadLocal(ThreadLocalKey key, uword value);
  static intptr_t GetMaxStackSize();
  static void Join(ThreadJoinId id);
  static intptr_t ThreadIdToIntPtr(ThreadId id);
  static ThreadId ThreadIdFromIntPtr(intptr_t id);
  static bool Compare(ThreadId a, ThreadId b);
  static void GetThreadCpuUsage(ThreadId thread_id, int64_t* cpu_usage);

  // Called at VM startup and shutdown.
  static void InitOnce();

  static bool IsThreadInList(ThreadId join_id);

  static const intptr_t kStackSizeBuffer = (4 * KB * kWordSize);

  static ThreadLocalKey kUnsetThreadLocalKey;
  static ThreadId kInvalidThreadId;
  static ThreadJoinId kInvalidThreadJoinId;

 private:
  // These methods should not be used in a generic way and hence
  // are private, they have been added to solve the problem of
  // accessing the VM thread structure from an OSThread object
  // in the windows thread interrupter which is used for profiling.
  // We could eliminate this requirement if the windows thread interrupter
  // is implemented differently.
  Thread* thread() const { return thread_; }
  void set_thread(Thread* value) {
    thread_ = value;
  }

  static void Cleanup();
  static ThreadId GetCurrentThreadTraceId();
  static ThreadJoinId GetCurrentThreadJoinId();
  static OSThread* GetOSThreadFromThread(Thread* thread);
  static void AddThreadToList(OSThread* thread);
  static void RemoveThreadFromList(OSThread* thread);

  static ThreadLocalKey thread_key_;

  const ThreadId id_;
  const ThreadId join_id_;
  const ThreadId trace_id_;  // Used to interface with tracing tools.
  char* name_;  // A name for this thread.

  Mutex* timeline_block_lock_;
  TimelineEventBlock* timeline_block_;

  // All |Thread|s are registered in the thread list.
  OSThread* thread_list_next_;

  uintptr_t thread_interrupt_disabled_;
  Log* log_;
  uword stack_base_;
  Thread* thread_;

  static OSThread* thread_list_head_;
  static Mutex* thread_list_lock_;

  friend class OSThreadIterator;
  friend class ThreadInterrupterWin;
  friend class ThreadRegistry;
};


// Note that this takes the thread list lock, prohibiting threads from coming
// on- or off-line.
class OSThreadIterator : public ValueObject {
 public:
  OSThreadIterator();
  ~OSThreadIterator();

  // Returns false when there are no more threads left.
  bool HasNext() const;

  // Returns the current thread and moves forward.
  OSThread* Next();

 private:
  OSThread* next_;
};


class Mutex {
 public:
  Mutex();
  ~Mutex();

  void Lock();
  bool TryLock();
  void Unlock();

#if defined(DEBUG)
  bool IsOwnedByCurrentThread() const {
    return owner_ == OSThread::GetCurrentThreadId();
  }
#else
  bool IsOwnedByCurrentThread() const {
    UNREACHABLE();
    return false;
  }
#endif

 private:
  MutexData data_;
#if defined(DEBUG)
  ThreadId owner_;
#endif  // defined(DEBUG)

  DISALLOW_COPY_AND_ASSIGN(Mutex);
};


class Monitor {
 public:
  enum WaitResult {
    kNotified,
    kTimedOut
  };

  static const int64_t kNoTimeout = 0;

  Monitor();
  ~Monitor();

  void Enter();
  void Exit();

  // Wait for notification or timeout.
  WaitResult Wait(int64_t millis);
  WaitResult WaitMicros(int64_t micros);

  // Notify waiting threads.
  void Notify();
  void NotifyAll();

#if defined(DEBUG)
  bool IsOwnedByCurrentThread() const {
    return owner_ == OSThread::GetCurrentThreadId();
  }
#else
  bool IsOwnedByCurrentThread() const {
    UNREACHABLE();
    return false;
  }
#endif

 private:
  MonitorData data_;  // OS-specific data.
#if defined(DEBUG)
  ThreadId owner_;
#endif  // defined(DEBUG)

  DISALLOW_COPY_AND_ASSIGN(Monitor);
};


}  // namespace dart


#endif  // VM_OS_THREAD_H_
