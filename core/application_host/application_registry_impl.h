// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_COMMON_APPLICATION_REGISTRY_IMPL_H_
#define CORE_COMMON_APPLICATION_REGISTRY_IMPL_H_

#include "base/containers/hash_tables.h"
#include "core/public/application_host/application_registry.h"

namespace core {

class ApplicationRegistryImpl : public ApplicationRegistry {
 public:
  ApplicationRegistryImpl();
  ~ApplicationRegistryImpl() override;

  void RegisterApplication(const GURL& url,
                           scoped_ptr<ApplicationLoader> loader) override;

  ApplicationLoader* GetApplicationLoader(const GURL& url) override;

 private:
  base::hash_map<std::string, ApplicationLoader*> loaders_;
};

}  // namespace core

#endif  // CORE_COMMON_APPLICATION_REGISTRY_IMPL_H_
