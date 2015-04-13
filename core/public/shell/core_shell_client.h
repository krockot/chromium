// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_SHELL_CORE_SHELL_CLIENT_H_
#define CORE_PUBLIC_SHELL_CORE_SHELL_CLIENT_H_

#include "base/macros.h"
#include "url/gurl.h"

namespace core {

class CoreShellClient {
 public:
  CoreShellClient() {}
  virtual ~CoreShellClient() {}

  static CoreShellClient* Get();

  virtual GURL GetStartupURL() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(CoreShellClient);
};

}  // namespace core

#endif  // CORE_PUBLIC_SHELL_CORE_SHELL_CLIENT_H_
