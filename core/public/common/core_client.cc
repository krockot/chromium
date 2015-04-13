// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/public/common/core_client.h"

namespace core {

namespace {

static CoreClient* g_client;

static CoreShellClient* g_shell_client_for_test;
static CoreApplicationHostClient* g_application_host_client_for_test;

}  // namespace

CoreShellClient* CoreClient::shell() const {
  if (g_shell_client_for_test)
    return g_shell_client_for_test;
  return shell_client_;
}

CoreApplicationHostClient* CoreClient::application_host() const {
  if (g_application_host_client_for_test)
    return g_application_host_client_for_test;
  return application_host_client_;
}

CoreClient* CoreClient::Get() {
  return g_client;
}

void CoreClient::Set(CoreClient* client) {
  if (!g_client) {
    g_client = client;
  }
}

void CoreClient::SetForTest(CoreClient* client) {
  g_client = client;
}

// static
void CoreClient::SetShellClientForTest(CoreShellClient* shell_client) {
  g_shell_client_for_test = shell_client;
}

// static
void CoreClient::SetApplicationHostClientForTest(
    CoreApplicationHostClient* application_host_client) {
  g_application_host_client_for_test = application_host_client;
}

void CoreClient::SetShellClient(CoreShellClient* shell_client) {
  shell_client_ = shell_client;
}

void CoreClient::SetApplicationHostClient(
    CoreApplicationHostClient* application_host_client) {
  application_host_client_ = application_host_client;
}

}  // namespace core
