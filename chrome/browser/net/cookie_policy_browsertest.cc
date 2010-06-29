// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task.h"
#include "base/waitable_event.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/host_content_settings_map.h"
#include "chrome/browser/profile.h"
#include "chrome/common/net/url_request_context_getter.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "net/base/mock_host_resolver.h"

namespace {

class GetCookiesTask : public Task {
 public:
  GetCookiesTask(const GURL& url,
                 URLRequestContextGetter* context_getter,
                 base::WaitableEvent* event,
                 std::string* cookies)
      : url_(url),
        context_getter_(context_getter),
        event_(event),
        cookies_(cookies) {}

  virtual void Run() {
    *cookies_ = context_getter_->GetCookieStore()->GetCookies(url_);
    event_->Signal();
  }

 private:
  const GURL& url_;
  URLRequestContextGetter* const context_getter_;
  base::WaitableEvent* const event_;
  std::string* const cookies_;

  DISALLOW_COPY_AND_ASSIGN(GetCookiesTask);
};

class CookiePolicyBrowserTest : public InProcessBrowserTest {
 protected:
  CookiePolicyBrowserTest() {}

  std::string GetCookies(const GURL& url) {
    std::string cookies;
    base::WaitableEvent event(true /* manual reset */,
                              false /* not initially signaled */);
    URLRequestContextGetter* context_getter =
        browser()->profile()->GetRequestContext();
    EXPECT_TRUE(
        ChromeThread::PostTask(
            ChromeThread::IO, FROM_HERE,
            new GetCookiesTask(url, context_getter, &event, &cookies)));
    EXPECT_TRUE(event.Wait());
    return cookies;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CookiePolicyBrowserTest);
};

// Visits a page that sets a first-party cookie.
IN_PROC_BROWSER_TEST_F(CookiePolicyBrowserTest, AllowFirstPartyCookies) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server != NULL);

  browser()->profile()->GetHostContentSettingsMap()->
      SetBlockThirdPartyCookies(true);

  GURL url = server->TestServerPage("set-cookie?cookie1");

  std::string cookie = GetCookies(url);
  ASSERT_EQ("", cookie);

  ui_test_utils::NavigateToURL(browser(), url);

  cookie = GetCookies(url);
  EXPECT_EQ("cookie1", cookie);
}

// Visits a page that is a redirect across domain boundary to a page that sets
// a first-party cookie.
IN_PROC_BROWSER_TEST_F(CookiePolicyBrowserTest,
                       AllowFirstPartyCookiesRedirect) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server != NULL);

  browser()->profile()->GetHostContentSettingsMap()->
      SetBlockThirdPartyCookies(true);

  GURL url = server->TestServerPage("server-redirect?");

  GURL redirected_url = server->TestServerPage("set-cookie?cookie2");
  // Change the host name from localhost to www.example.com so it triggers
  // third-party cookie blocking if the first party for cookies URL is not
  // changed when we follow a redirect.
  ASSERT_EQ("localhost", redirected_url.host());
  GURL::Replacements replacements;
  std::string new_host("www.example.com");
  replacements.SetHostStr(new_host);
  redirected_url = redirected_url.ReplaceComponents(replacements);

  std::string cookie = GetCookies(redirected_url);
  ASSERT_EQ("", cookie);

  host_resolver()->AddRule("www.example.com", "127.0.0.1");

  ui_test_utils::NavigateToURL(browser(),
                               GURL(url.spec() + redirected_url.spec()));

  cookie = GetCookies(redirected_url);
  EXPECT_EQ("cookie2", cookie);
}

}  // namespace
