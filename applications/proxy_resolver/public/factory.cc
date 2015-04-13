// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "applications/proxy_resolver/public/factory.h"

#include "applications/proxy_resolver/proxy_resolver.h"

namespace proxy_resolver {

namespace {

scoped_ptr<core::ApplicationDelegate> CreateDelegate() {
  return scoped_ptr<core::ApplicationDelegate>(new ProxyResolver);
}
}

core::ApplicationFactory CreateFactory() {
  return base::Bind(&CreateDelegate);
}

}  // namespace proxy_resolver
