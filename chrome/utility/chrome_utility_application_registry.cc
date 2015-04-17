// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/utility/chrome_utility_application_registry.h"

#include "base/stl_util.h"
#include "build/build_config.h"
#include "core/public/application_host/application_loader.h"
#include "core/public/common/application_urls.h"
#include "url/gurl.h"

#if !defined(OS_ANDROID)
#include "applications/proxy_resolver/public/factory.h"
#endif

ChromeUtilityApplicationRegistry::ChromeUtilityApplicationRegistry() {
  PopulateApplicationMap();
}

ChromeUtilityApplicationRegistry::~ChromeUtilityApplicationRegistry() {
  STLDeleteContainerPairSecondPointers(application_map_.begin(),
                                       application_map_.end());
}

core::ApplicationLoader* ChromeUtilityApplicationRegistry::GetApplicationLoader(
    const GURL& url) {
  auto iter = application_map_.find(url);
  if (iter == application_map_.end())
    return nullptr;
  return iter->second;
}

void ChromeUtilityApplicationRegistry::AddApplication(
    const GURL& url,
    scoped_ptr<core::ApplicationLoader> loader) {
  auto result = application_map_.insert(std::make_pair(url, loader.release()));
  DCHECK(result.second);
}

void ChromeUtilityApplicationRegistry::PopulateApplicationMap() {
#if !defined(OS_ANDROID)
  // Proxy Auto-Config application.
  AddApplication(GURL(core::kSystemProxyResolverUrl),
                 core::ApplicationLoader::CreateForFactory(
                     proxy_resolver::CreateFactory()));
#endif
}
