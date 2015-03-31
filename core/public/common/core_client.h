// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_CORE_CLIENT_H_
#define CORE_PUBLIC_COMMON_CORE_CLIENT_H_

#include "base/macros.h"

namespace core {

class CoreApplicationHostClient;

class CoreClient {
 public:
  CoreClient() {}
  virtual ~CoreClient() {}

  CoreApplicationHostClient* application_host() const;

  static CoreClient* Get();
  static void Set(CoreClient* client);
  static void SetForTest(CoreClient* client);

  static void SetApplicationHostClientForTest(
      CoreApplicationHostClient* application_host_client);

 protected:
  void SetApplicationHostClient(
      CoreApplicationHostClient* application_host_client);

 private:
  CoreApplicationHostClient* application_host_client_;

  DISALLOW_COPY_AND_ASSIGN(CoreClient);
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_CORE_CLIENT_H_
