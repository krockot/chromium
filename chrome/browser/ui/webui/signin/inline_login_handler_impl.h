// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SIGNIN_INLINE_LOGIN_HANDLER_IMPL_H_
#define CHROME_BROWSER_UI_WEBUI_SIGNIN_INLINE_LOGIN_HANDLER_IMPL_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/sync/one_click_signin_sync_starter.h"
#include "chrome/browser/ui/webui/signin/inline_login_handler.h"

class GaiaAuthFetcher;

// Implementation for the inline login WebUI handler on desktop Chrome. Once
// CrOS migrates to the same webview approach as desktop Chrome, much of the
// code in this class should move to its base class |InlineLoginHandler|.
class InlineLoginHandlerImpl : public InlineLoginHandler,
                               public content::WebContentsObserver {
 public:
  InlineLoginHandlerImpl();
  ~InlineLoginHandlerImpl() override;

  using InlineLoginHandler::web_ui;

  base::WeakPtr<InlineLoginHandlerImpl> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  Browser* GetDesktopBrowser();
  void SyncStarterCallback(OneClickSigninSyncStarter::SyncSetupResult result);
  // Closes the current tab and shows the account management view of the avatar
  // bubble if |show_account_management| is true.
  void CloseTab(bool show_account_management);
  void HandleLoginError(const std::string& error_msg);

 private:
  friend class InlineLoginUIBrowserTest;
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferNoProfile);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOffer);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferProfileConnected);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest,
                           CanOfferUsernameNotAllowed);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferWithRejectedEmail);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferNoSigninCookies);

  // Argument to CanOffer().
  enum CanOfferFor {
    CAN_OFFER_FOR_ALL,
    CAN_OFFER_FOR_SECONDARY_ACCOUNT
  };

  static bool CanOffer(Profile* profile,
                       CanOfferFor can_offer_for,
                       const std::string& email,
                       std::string* error_message);

  // InlineLoginHandler overrides:
  void SetExtraInitParams(base::DictionaryValue& params) override;
  void CompleteLogin(const base::ListValue* args) override;

  // Overridden from content::WebContentsObserver overrides.
  void DidCommitProvisionalLoadForFrame(
      content::RenderFrameHost* render_frame_host,
      const GURL& url,
      ui::PageTransition transition_type) override;

  // True if the user has navigated to untrusted domains during the signin
  // process.
  bool confirm_untrusted_signin_;

  base::WeakPtrFactory<InlineLoginHandlerImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(InlineLoginHandlerImpl);
};

#endif  // CHROME_BROWSER_UI_WEBUI_SIGNIN_INLINE_LOGIN_HANDLER_IMPL_H_
