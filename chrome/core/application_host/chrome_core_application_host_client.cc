// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/core/application_host/chrome_core_application_host_client.h"

#include "chrome/core/application_host/chrome_application_registry.h"

ChromeCoreApplicationHostClient::ChromeCoreApplicationHostClient() {
}

ChromeCoreApplicationHostClient::~ChromeCoreApplicationHostClient() {
}

scoped_ptr<core::ApplicationRegistry>
ChromeCoreApplicationHostClient::CreateApplicationRegistry() {
  return scoped_ptr<core::ApplicationRegistry>(new ChromeApplicationRegistry());
}
