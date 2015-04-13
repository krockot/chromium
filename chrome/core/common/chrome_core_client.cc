// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/core/common/chrome_core_client.h"

#include "base/lazy_instance.h"
#include "chrome/core/application_host/chrome_core_application_host_client.h"
#include "chrome/core/shell/chrome_core_shell_client.h"

namespace {

base::LazyInstance<ChromeCoreShellClient> g_shell_client =
    LAZY_INSTANCE_INITIALIZER;

base::LazyInstance<ChromeCoreApplicationHostClient> g_application_host_client =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

ChromeCoreClient::ChromeCoreClient() {
  SetShellClient(g_shell_client.Pointer());
  SetApplicationHostClient(g_application_host_client.Pointer());
}

ChromeCoreClient::~ChromeCoreClient() {
}
