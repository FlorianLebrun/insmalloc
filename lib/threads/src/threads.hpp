#pragma once
#include <sat/memory/system-object.hpp>
#include <sat/threads/thread.hpp>
#include <sat/threads/spinlock.hpp>
#include <boost/thread.hpp>

namespace sat {
   namespace impl {

      class BasicThread;
      class BasicThreadPool;
      class DefaultThread;
      class DefaultThreadPool;

      class BasicThread : public SystemObject<sat::Thread>::Derived<BasicThread> {
      public:
         BasicThreadPool* pool;
         boost::thread thread;
         BasicThread(BasicThreadPool* pool, std::function<void()>&& entrypoint);
         virtual uint64_t getID() final;
         virtual uintptr_t getNativeHandle() final;
      };

      class BasicThreadPool : public SystemObject<sat::ThreadPool>::Derived<BasicThreadPool> {
         friend BasicThread;
      protected:
         SpinLock lock;
         std::vector<BasicThread*> threads;
         boost::thread::attributes attrs;
      public:
         BasicThreadPool(int stacksize = 0);
         virtual ~BasicThreadPool();
         virtual void foreach(std::function<void(Thread*)>&& callback) override final;
         virtual Thread* create(std::function<void()>&& entrypoint) override;
      };

      class DefaultThread : public SystemObject<sat::Thread>::Derived<DefaultThread> {
      public:
         DefaultThreadPool* pool;
         void* threadHandle;
         uint64_t threadId;
         DefaultThread();
         ~DefaultThread();
         virtual uint64_t getID() final;
         virtual uintptr_t getNativeHandle() final;
      };

      class DefaultThreadPool : public SystemObject<sat::ThreadPool>::Derived<DefaultThreadPool> {
         friend DefaultThread;
      protected:
         SpinLock lock;
         std::vector<DefaultThread*> threads;
      public:
         virtual void foreach(std::function<void(Thread*)>&& callback) override final;
         virtual Thread* create(std::function<void()>&& entrypoint) override;
      };
   }
}

