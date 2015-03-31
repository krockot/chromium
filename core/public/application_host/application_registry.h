// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_
#define CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "core/public/application_host/application_loader.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class ApplicationRegistry {
 public:
  virtual ~ApplicationRegistry() {}

  virtual void RegisterApplication(const std::string& path,
                                   scoped_ptr<ApplicationLoader> loader) = 0;

  virtual ApplicationLoader* GetApplicationLoader(const std::string& path) = 0;
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_APPLICATION_REGISTRY_H_
