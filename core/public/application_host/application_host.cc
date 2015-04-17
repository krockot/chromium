// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/public/application_host/application_host.h"

#include "core/application_host/application_host_impl.h"
#include "core/public/application_host/application_registry.h"

namespace core {

ApplicationHost::ApplicationHost() {
}

ApplicationHost::~ApplicationHost() {
}

// static
scoped_ptr<ApplicationHost> ApplicationHost::Create(
    scoped_ptr<ApplicationRegistry> registry) {
  return scoped_ptr<ApplicationHost>(new ApplicationHostImpl(registry.Pass()));
}

}  // namespace core
