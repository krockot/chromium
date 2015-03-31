// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/public/application/application_runner.h"

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/debug/stack_trace.h"
#include "base/message_loop/message_loop.h"
#include "core/public/application/application.h"
#include "core/public/application/application_delegate.h"
#include "mojo/common/message_pump_mojo.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/cpp/system/handle.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class ApplicationRunner::RunState {
 public:
  RunState();
  ~RunState();

  base::MessageLoop* message_loop() { return message_loop_.get(); }

 private:
  base::AtExitManager at_exit_;
  scoped_ptr<base::MessageLoop> message_loop_;
};

ApplicationRunner::RunState::RunState() {
  base::CommandLine::Init(0, NULL);
#ifndef NDEBUG
  base::debug::EnableInProcessStackDumping();
#endif

  message_loop_.reset(new base::MessageLoop(
      mojo::common::MessagePumpMojo::Create()));
}

ApplicationRunner::RunState::~RunState() {
}

ApplicationRunner::ApplicationRunner() : run_state_(new RunState()) {
}

ApplicationRunner::~ApplicationRunner() {
}

MojoResult ApplicationRunner::Run(
    scoped_ptr<ApplicationDelegate> delegate,
    MojoHandle application_request_handle) {
  {
    scoped_ptr<Application> application = Application::Create(
        delegate.Pass(),
        mojo::MakeRequest<mojo::Application>(mojo::MakeScopedHandle(
            mojo::MessagePipeHandle(application_request_handle))));
    run_state_->message_loop()->Run();
  }
  return MOJO_RESULT_OK;
}

}  // namespace core
