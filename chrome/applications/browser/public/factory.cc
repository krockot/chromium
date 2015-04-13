// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/applications/browser/public/factory.h"

#include "chrome/applications/browser/browser.h"

namespace chrome_browser {

namespace {

scoped_ptr<core::ApplicationDelegate> CreateDelegate() {
  return scoped_ptr<core::ApplicationDelegate>(new Browser);
}
}

core::ApplicationFactory CreateFactory() {
  return base::Bind(&CreateDelegate);
}

}  // namespace chrome_browser
