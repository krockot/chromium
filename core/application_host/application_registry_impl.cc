// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/application_registry_impl.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "url/gurl.h"

namespace core {

ApplicationRegistryImpl::ApplicationRegistryImpl() {
}

ApplicationRegistryImpl::~ApplicationRegistryImpl() {
  STLDeleteContainerPairSecondPointers(loaders_.begin(), loaders_.end());
}

void ApplicationRegistryImpl::RegisterApplication(
    const GURL& url,
    scoped_ptr<ApplicationLoader> loader) {
  auto result = loaders_.insert(
      std::make_pair(std::string(url.spec()), loader.release()));
  DCHECK(result.second) << "Registered duplicate application: " << url.spec();
}

ApplicationLoader* ApplicationRegistryImpl::GetApplicationLoader(
    const GURL& url) {
  auto iter = loaders_.find(url.spec());
  if (iter == loaders_.end()) {
    return nullptr;
  }
  return iter->second;
}

}  // namespace core
