// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_CLIPBOARD_AURA_H_
#define REMOTING_HOST_CLIPBOARD_AURA_H_

#include "base/memory/scoped_ptr.h"
#include "base/single_thread_task_runner.h"
#include "remoting/host/clipboard.h"

namespace remoting {

namespace protocol {
class ClipboardStub;
}  // namespace protocol

// On Chrome OS, the clipboard is managed by aura instead of the underlying
// native platform (e.g. x11, ozone, etc).
//
// This class (1) monitors the aura clipboard for changes and notifies the
// |client_clipboard|, and (2) provides an interface to inject clipboard event
// into aura.
//
// The public API of this class can be called in any thread as internally it
// always posts the call to the |ui_task_runner|.  On ChromeOS, that should
// be the UI thread of the browser process.
//
class ClipboardAura : public Clipboard {
 public:
  explicit ClipboardAura(
      scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner);
  ~ClipboardAura() override;

  // Clipboard interface.
  void Start(scoped_ptr<protocol::ClipboardStub> client_clipboard) override;
  void InjectClipboardEvent(const protocol::ClipboardEvent& event) override;
  void Stop() override;

  // Overrides the clipboard polling interval for unit test.
  void SetPollingIntervalForTesting(base::TimeDelta polling_interval);

 private:
  class Core;

  scoped_ptr<Core> core_;
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ClipboardAura);
};

}  // namespace remoting

#endif  // REMOTING_HOST_CLIPBOARD_AURA_H_
