// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/core/application_host/chrome_application_registry.h"

#include "applications/proxy_resolver/public/factory.h"
#include "base/stl_util.h"
#include "chrome/applications/browser/public/factory.h"
#include "chrome/core/common/application_urls.h"
#include "core/public/application_host/application_loader.h"
#include "url/gurl.h"

ChromeApplicationRegistry::ChromeApplicationRegistry() {
  PopulateApplicationMap();
}

ChromeApplicationRegistry::~ChromeApplicationRegistry() {
  STLDeleteContainerPairSecondPointers(
      application_map_.begin(), application_map_.end());
}

core::ApplicationLoader* ChromeApplicationRegistry::GetApplicationLoader(
    const GURL& url) {
  auto iter = application_map_.find(url);
  if (iter == application_map_.end())
    return nullptr;
  return iter->second;
}

void ChromeApplicationRegistry::AddApplication(
    const GURL& url, scoped_ptr<core::ApplicationLoader> loader) {
  auto result = application_map_.insert(std::make_pair(url, loader.release()));
  DCHECK(result.second);
}

void ChromeApplicationRegistry::PopulateApplicationMap() {
  // Main browser application. This is used as the shell's startup application
  // when running Chrome.
  AddApplication(GURL(application_urls::kBrowser),
                 core::ApplicationLoader::CreateForFactory(
                    chrome_browser::CreateFactory()));

  // Proxy Auto-Config application.
  AddApplication(GURL(application_urls::kProxyResolver),
                 core::ApplicationLoader::CreateForFactory(
                    proxy_resolver::CreateFactory()));
}
