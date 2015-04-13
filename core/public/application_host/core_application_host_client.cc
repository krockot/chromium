// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/public/application_host/core_application_host_client.h"

#include "core/public/common/core_client.h"

namespace core {

// static
CoreApplicationHostClient* CoreApplicationHostClient::Get() {
  return core::CoreClient::Get()->application_host();
}

}  // namespace core
