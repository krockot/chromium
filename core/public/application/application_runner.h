// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_APPLICATION_RUNNER_H_
#define CORE_PUBLIC_APPLICATION_APPLICATION_RUNNER_H_

#include "base/memory/scoped_ptr.h"
#include "third_party/mojo/src/mojo/public/c/system/types.h"

namespace core {

class Application;
class ApplicationDelegate;

class ApplicationRunner {
 public:
  ApplicationRunner();
  ~ApplicationRunner();

  MojoResult Run(scoped_ptr<ApplicationDelegate> delegate,
                 MojoHandle application_request_handle);

 private:
  class RunState;
  scoped_ptr<RunState> run_state_;
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_APPLICATION_RUNNER_H_
