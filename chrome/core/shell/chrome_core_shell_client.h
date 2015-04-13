// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_CORE_SHELL_CHROME_CORE_SHELL_CLIENT_H_
#define CHROME_CORE_SHELL_CHROME_CORE_SHELL_CLIENT_H_

#include "base/macros.h"
#include "core/public/shell/core_shell_client.h"

class ChromeCoreShellClient : public core::CoreShellClient {
 public:
  ChromeCoreShellClient();
  ~ChromeCoreShellClient() override;

 private:
  // core::CoreShellClient:
  GURL GetStartupURL() override;

  DISALLOW_COPY_AND_ASSIGN(ChromeCoreShellClient);
};

#endif  // CHROME_CORE_SHELL_CHROME_CORE_SHELL_CLIENT_H_
