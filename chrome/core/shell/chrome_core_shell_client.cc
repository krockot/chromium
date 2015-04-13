// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/core/shell/chrome_core_shell_client.h"

ChromeCoreShellClient::ChromeCoreShellClient() {
}

ChromeCoreShellClient::~ChromeCoreShellClient() {
}

GURL ChromeCoreShellClient::GetStartupURL() {
  return GURL("system:browser");
}
