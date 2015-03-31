// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_CORE_APPLICATION_HOST_CHROME_CORE_APPLICATION_HOST_CLIENT_H_
#define CHROME_CORE_APPLICATION_HOST_CHROME_CORE_APPLICATION_HOST_CLIENT_H_

#include "core/public/application_host/core_application_host_client.h"

class ChromeCoreApplicationHostClient : public core::CoreApplicationHostClient {
 public:
  ChromeCoreApplicationHostClient();
  ~ChromeCoreApplicationHostClient() override;

  void RegisterApplications(core::ApplicationRegistry* registry) override;
  scoped_refptr<base::TaskRunner> GetTaskRunnerForLibraryLoad() override;
};

#endif  // CHROME_CORE_APPLICATION_HOST_CHROME_CORE_APPLICATION_HOST_CLIENT_H_
