// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_CORE_COMMON_CHROME_CORE_CLIENT_H_
#define CHROME_CORE_COMMON_CHROME_CORE_CLIENT_H_

#include "core/public/common/core_client.h"

class ChromeCoreClient : public core::CoreClient {
 public:
  ChromeCoreClient();
  ~ChromeCoreClient() override;
};

#endif  // CHROME_CORE_COMMON_CHROME_CORE_CLIENT_H_
