// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_CORE_APPLICATION_HOST_CLIENT_H_
#define CORE_PUBLIC_APPLICATION_HOST_CORE_APPLICATION_HOST_CLIENT_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"

namespace core {

class ApplicationRegistry;

class CoreApplicationHostClient {
 public:
  CoreApplicationHostClient() {}
  virtual ~CoreApplicationHostClient() {}

  static CoreApplicationHostClient* Get();

  virtual scoped_ptr<ApplicationRegistry> CreateApplicationRegistry() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(CoreApplicationHostClient);
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_CORE_APPLICATION_HOST_CLIENT_H_
