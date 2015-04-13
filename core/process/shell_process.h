// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PROCESS_SHELL_PROCESS_H_
#define CORE_PROCESS_SHELL_PROCESS_H_

#include "core/process/process_base.h"

#include "base/macros.h"

namespace core {

// Process type used exclusively by the main Shell process.
class ShellProcess : public ProcessBase {
 public:
  ShellProcess();
  ~ShellProcess() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ShellProcess);
};

}  // namespace core

#endif  // CORE_PROCESS_SHELL_PROCESS_H_
