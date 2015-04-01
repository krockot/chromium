// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/application_registry_impl.h"

#include "base/logging.h"

namespace core {

ApplicationRegistryImpl::ApplicationRegistryImpl() {
}

ApplicationRegistryImpl::~ApplicationRegistryImpl() {
}

void ApplicationRegistryImpl::RegisterApplication(
    const std::string& path,
    scoped_ptr<ApplicationLoader> loader) {
  auto result = loaders_.insert(std::make_pair<std::string, ApplicationLoader*>(
      std::string(path), loader.release()));
  DCHECK(result.second) << "Registered duplicate application: " << path;
}

ApplicationLoader* ApplicationRegistryImpl::GetApplicationLoader(
    const std::string& path) {
  auto iter = loaders_.find(path);
  if (iter == loaders_.end()) {
    return nullptr;
  }
  return iter->second;
}

}  // namespace core
