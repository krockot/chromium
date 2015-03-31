// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_CORE_APPLICATION_HOST_CLIENT_H_
#define CORE_PUBLIC_APPLICATION_HOST_CORE_APPLICATION_HOST_CLIENT_H_

#include "base/memory/ref_counted.h"
#include "base/task_runner.h"

namespace core {

class ApplicationRegistry;

class CoreApplicationHostClient {
 public:
  virtual ~CoreApplicationHostClient() {}

  static CoreApplicationHostClient* Get();

  virtual void RegisterApplications(ApplicationRegistry* registry) = 0;
  virtual scoped_refptr<base::TaskRunner> GetTaskRunnerForLibraryLoad() = 0;
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_CORE_APPLICATION_HOST_CLIENT_H_
