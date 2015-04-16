// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_
#define CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"

class GURL;

namespace core {

class ApplicationLoader;

// This object acts as a mapping between raw system application URLs and
// ApplicationLoaders which can be used to launch new instances of the
// corresponding application. A global instance of this should be |Set| by
// the embedder within any process that will create ApplicationHosts.
class ApplicationRegistry {
 public:
  ApplicationRegistry();
  virtual ~ApplicationRegistry();

  static ApplicationRegistry* Get();
  static void Set(ApplicationRegistry* registry);

  virtual ApplicationLoader* GetApplicationLoader(const GURL& url) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ApplicationRegistry);
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_
