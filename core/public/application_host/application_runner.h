// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_APPLICATION_RUNNER_H_
#define CORE_PUBLIC_APPLICATION_HOST_APPLICATION_RUNNER_H_

#include "base/callback_forward.h"
#include "base/macros.h"

namespace core {

class ApplicationRunner {
 public:
  using ExitCallback = base::Closure;

  ApplicationRunner() {}
  virtual ~ApplicationRunner() {}

  virtual void Start(const ExitCallback& exit_callback) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ApplicationRunner);
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_APPLICATION_RUNNER_H_
