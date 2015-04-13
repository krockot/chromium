// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_APPLICATION_HOST_IN_THREAD_APPLICATION_RUNNER_H_
#define CORE_APPLICATION_HOST_IN_THREAD_APPLICATION_RUNNER_H_

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/application/application.h"
#include "core/public/application_host/application_runner.h"

namespace core {

// A primitive ApplicationRunner which observes the Application on the current
// thread.
class InThreadApplicationRunner : public ApplicationRunner,
                                  public Application::Observer {
 public:
  InThreadApplicationRunner(scoped_ptr<Application> application);
  ~InThreadApplicationRunner() override;

  void Start(const ExitCallback& exit_callback) override;

 private:
  // Application::Observer:
  void OnQuit() override;

  scoped_ptr<Application> application_;
  ExitCallback exit_callback_;

  DISALLOW_COPY_AND_ASSIGN(InThreadApplicationRunner);
};

}  // namespace core

#endif  // CORE_APPLICATION_HOST_IN_THREAD_APPLICATION_RUNNER_H_
