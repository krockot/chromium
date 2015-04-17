// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UTILITY_CHROME_UTILITY_APPLICATION_REGISTRY_
#define CHROME_UTILITY_CHROME_UTILITY_APPLICATION_REGISTRY_

#include <map>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/application_host/application_registry.h"
#include "url/gurl.h"

class ChromeUtilityApplicationRegistry : public core::ApplicationRegistry {
 public:
  ChromeUtilityApplicationRegistry();
  ~ChromeUtilityApplicationRegistry() override;

  core::ApplicationLoader* GetApplicationLoader(const GURL& url) override;

 private:
  void AddApplication(const GURL& url,
                      scoped_ptr<core::ApplicationLoader> loader);
  void PopulateApplicationMap();

  std::map<GURL, core::ApplicationLoader*> application_map_;

  DISALLOW_COPY_AND_ASSIGN(ChromeUtilityApplicationRegistry);
};

#endif  // CHROME_UTILITY_CHROME_UTILITY_APPLICATION_REGISTRY_
