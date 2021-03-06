// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_HTML_VIEWER_WEBTHREAD_IMPL_H_
#define MOJO_SERVICES_HTML_VIEWER_WEBTHREAD_IMPL_H_

#include <map>

#include "base/memory/scoped_ptr.h"
#include "base/threading/thread.h"
#include "mojo/services/html_viewer/webscheduler_impl.h"
#include "third_party/WebKit/public/platform/WebThread.h"

namespace html_viewer {

class WebThreadBase : public blink::WebThread {
 public:
  virtual ~WebThreadBase();

  virtual void addTaskObserver(TaskObserver* observer);
  virtual void removeTaskObserver(TaskObserver* observer);

  virtual bool isCurrentThread() const = 0;

 protected:
  WebThreadBase();

 private:
  class TaskObserverAdapter;

  typedef std::map<TaskObserver*, TaskObserverAdapter*> TaskObserverMap;
  TaskObserverMap task_observer_map_;
};

class WebThreadImpl : public WebThreadBase {
 public:
  explicit WebThreadImpl(const char* name);
  ~WebThreadImpl() override;

  virtual void postTask(const blink::WebTraceLocation& location, Task* task);
  virtual void postDelayedTask(const blink::WebTraceLocation& location,
                               Task* task,
                               long long delay_ms);

  virtual void enterRunLoop();
  virtual void exitRunLoop();

  virtual blink::WebScheduler* scheduler() const;

  base::MessageLoop* message_loop() const { return thread_->message_loop(); }

  bool isCurrentThread() const override;
  virtual blink::PlatformThreadId threadId() const;

 private:
  scoped_ptr<base::Thread> thread_;
  scoped_ptr<WebSchedulerImpl> web_scheduler_;
};

class WebThreadImplForMessageLoop : public WebThreadBase {
 public:
  explicit WebThreadImplForMessageLoop(
      base::MessageLoopProxy* message_loop);
  ~WebThreadImplForMessageLoop() override;

  virtual void postTask(const blink::WebTraceLocation& location, Task* task);
  virtual void postDelayedTask(const blink::WebTraceLocation& location,
                               Task* task,
                               long long delay_ms);

  virtual void enterRunLoop();
  virtual void exitRunLoop();

  virtual blink::WebScheduler* scheduler() const;

 private:
  bool isCurrentThread() const override;
  virtual blink::PlatformThreadId threadId() const;

  scoped_refptr<base::MessageLoopProxy> message_loop_;
  scoped_ptr<WebSchedulerImpl> web_scheduler_;
  blink::PlatformThreadId thread_id_;
};

}  // namespace html_viewer

#endif  // MOJO_SERVICES_HTML_VIEWER_WEBTHREAD_IMPL_H_
