// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_
#define CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_

#include "base/macros.h"

class GURL;

namespace core {

class ApplicationLoader;

// This object acts as a mapping between raw system application URLs and
// ApplicationLoaders which can be used to launch new instances of the
// corresponding application. Every ApplicationHost must be provided with its
// own ApplicationRegistry instance by the embedder.
class ApplicationRegistry {
 public:
  ApplicationRegistry();
  virtual ~ApplicationRegistry();

  virtual ApplicationLoader* GetApplicationLoader(const GURL& url) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ApplicationRegistry);
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_
