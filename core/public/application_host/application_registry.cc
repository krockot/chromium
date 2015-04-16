// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/public/application_host/application_registry.h"

#include "base/lazy_instance.h"
#include "base/memory/scoped_ptr.h"

namespace core {

namespace {

base::LazyInstance<scoped_ptr<ApplicationRegistry>> g_application_registry =
    LAZY_INSTANCE_INITIALIZER;

}

ApplicationRegistry::ApplicationRegistry() {
}

ApplicationRegistry::~ApplicationRegistry() {
}

// static
ApplicationRegistry* ApplicationRegistry::Get() {
  return g_application_registry.Get().get();
}

// static
void ApplicationRegistry::Set(ApplicationRegistry* registry) {
  DCHECK(!g_application_registry.Get());
  g_application_registry.Get().reset(registry);
}

}  // namespace core
