// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_CORE_APPLICATION_HOST_APPLICATION_REGISTRY_H_
#define CHROME_CORE_APPLICATION_HOST_APPLICATION_REGISTRY_H_

#include <map>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/application_host/application_registry.h"
#include "url/gurl.h"

class ChromeApplicationRegistry : public core::ApplicationRegistry {
 public:
  ChromeApplicationRegistry();
  ~ChromeApplicationRegistry() override;

  core::ApplicationLoader* GetApplicationLoader(const GURL& url) override;

 private:
  void AddApplication(const GURL& url,
                      scoped_ptr<core::ApplicationLoader> loader);
  void PopulateApplicationMap();

  std::map<GURL, core::ApplicationLoader*> application_map_;

  DISALLOW_COPY_AND_ASSIGN(ChromeApplicationRegistry);
};

#endif
