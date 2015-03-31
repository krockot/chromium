// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/core/application_host/chrome_core_application_host_client.h"

#include "content/public/browser/browser_thread.h"
#include "core/public/application_host/application_loader.h"
#include "core/public/application_host/application_registry.h"

namespace {

const char kAppInit[] = "init";
const char kAppNet[] = "net";

}  // namespace

ChromeCoreApplicationHostClient::ChromeCoreApplicationHostClient() {
}

ChromeCoreApplicationHostClient::~ChromeCoreApplicationHostClient() {
}

void ChromeCoreApplicationHostClient::RegisterApplications(
    core::ApplicationRegistry* registry) {
  registry->RegisterApplication(
      kAppInit,
      core::ApplicationLoader::CreateForLibrary("chrome_init"));

  registry->RegisterApplication(
      kAppNet,
      core::ApplicationLoader::CreateForLibrary("net_service"));
}

scoped_refptr<base::TaskRunner>
ChromeCoreApplicationHostClient::GetTaskRunnerForLibraryLoad() {
  return content::BrowserThread::GetMessageLoopProxyForThread(
      content::BrowserThread::FILE);
}
