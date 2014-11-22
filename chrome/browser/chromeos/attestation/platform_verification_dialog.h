// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ATTESTATION_PLATFORM_VERIFICATION_DIALOG_H_
#define CHROME_BROWSER_CHROMEOS_ATTESTATION_PLATFORM_VERIFICATION_DIALOG_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/strings/string16.h"
#include "chrome/browser/chromeos/attestation/platform_verification_flow.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/controls/styled_label_listener.h"
#include "ui/views/window/dialog_delegate.h"

namespace content {
class WebContents;
}

namespace chromeos {
namespace attestation {

// A tab-modal dialog UI to ask the user for PlatformVerificationFlow.
class PlatformVerificationDialog : public views::DialogDelegateView,
                                   public views::StyledLabelListener,
                                   public content::WebContentsObserver {
 public:
  // Initializes a tab-modal dialog for |web_contents| and shows it.
  static void ShowDialog(
      content::WebContents* web_contents,
      const PlatformVerificationFlow::Delegate::ConsentCallback& callback);

 protected:
  ~PlatformVerificationDialog() override;

 private:
  PlatformVerificationDialog(
      content::WebContents* web_contents,
      const base::string16& domain,
      const PlatformVerificationFlow::Delegate::ConsentCallback& callback);

  // views::DialogDelegate:
  bool Cancel() override;
  bool Accept() override;
  bool Close() override;
  base::string16 GetDialogButtonLabel(ui::DialogButton button) const override;

  // views::WidgetDelegate:
  ui::ModalType GetModalType() const override;

  // views::View:
  gfx::Size GetPreferredSize() const override;

  // views::StyledLabelListener:
  void StyledLabelLinkClicked(const gfx::Range& range,
                              int event_flags) override;

  // content::WebContentsObserver:
  void DidStartNavigationToPendingEntry(
      const GURL& url,
      content::NavigationController::ReloadType reload_type) override;

  base::string16 domain_;
  PlatformVerificationFlow::Delegate::ConsentCallback callback_;

  DISALLOW_COPY_AND_ASSIGN(PlatformVerificationDialog);
};

}  // namespace attestation
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_ATTESTATION_PLATFORM_VERIFICATION_DIALOG_H_
