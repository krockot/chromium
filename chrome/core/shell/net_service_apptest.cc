// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "chrome/core/application_host/chrome_core_application_host_client.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "core/public/application/application.h"
#include "core/public/application/application_delegate.h"
#include "core/public/application_host/application_loader.h"
#include "core/public/application_host/application_registry.h"
#include "core/public/application_host/entry_point.h"
#include "core/public/common/application_connection.h"
#include "core/public/common/core_client.h"
#include "core/public/shell/shell.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/interfaces/host_resolver_service.mojom.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"

////////////////////////////////////////////////////////////////////////////
// Future library code for core app tests

class TestCoreApplicationHostClient;
class TestApplicationLoader;

class CoreAppTest : public InProcessBrowserTest {
 public:
  virtual void RunApplication() = 0;

 protected:
  void WaitForAppExit();

  const std::vector<std::string>& args() const { return args_; }
  mojo::Shell* shell() { return shell_; }

 private:
  friend class TestApplicationLoader;
  friend class TestApplicationDelegate;

  void Initialize(mojo::Shell* shell, const std::vector<std::string>& args);
  void AcceptConnection(const GURL& requestor_url,
                        scoped_ptr<core::ApplicationConnection> connection,
                        const GURL& resolved_url);
  void Quit();
  void SetUp() override;

  std::vector<std::string> args_;
  mojo::Shell* shell_;
  scoped_ptr<TestCoreApplicationHostClient> test_application_host_client_;
  base::Closure quit_closure_;
};

class TestApplicationDelegate : public core::ApplicationDelegate {
 public:
  TestApplicationDelegate(CoreAppTest* test) : test_(test) {}
  ~TestApplicationDelegate() override {}

  void InitializeApplication(
      mojo::Shell* shell,
      const std::vector<std::string>& args) override {
    test_->Initialize(shell, args);
  }

  void AcceptConnection(const GURL& requestor_url,
                        scoped_ptr<core::ApplicationConnection> connection,
                        const GURL& resolved_url) override {
    test_->AcceptConnection(requestor_url, connection.Pass(), resolved_url);
  }

  void Quit() override {
    test_->Quit();
  }

 private:
  CoreAppTest* test_;
};

class TestApplicationLoader : public core::ApplicationLoader {
 public:
  TestApplicationLoader(CoreAppTest* test) : test_(test) {}

 private:
  void TestEntryPoint(MojoHandle application_request_handle) {
    mojo::InterfaceRequest<mojo::Application> application_request =
        mojo::MakeRequest<mojo::Application>(mojo::MakeScopedHandle(
            mojo::MessagePipeHandle(application_request_handle)));
    scoped_ptr<core::Application> application = core::Application::Create(
        make_scoped_ptr<core::ApplicationDelegate>(
            new TestApplicationDelegate(test_)),
        application_request.Pass());
    base::RunLoop run_loop;
    run_loop.Run();
    test_->quit_closure_.Run();
  }

  // core::ApplicationLoader:
  void Load(
      mojo::InterfaceRequest<mojo::Application> application_request,
      const LoadCallback& callback) override {
    callback.Run(
        CreateSuccessResult(application_request.Pass(),
                            base::Bind(&TestApplicationLoader::TestEntryPoint,
                                       base::Unretained(this))));
  }

  CoreAppTest* test_;
};

class TestCoreApplicationHostClient : public ChromeCoreApplicationHostClient {
 public:
  TestCoreApplicationHostClient(CoreAppTest* test) : test_(test) {}

 private:
  // ChromeCoreApplicationHostClient:
  void RegisterApplications(core::ApplicationRegistry* registry) override {
    ChromeCoreApplicationHostClient::RegisterApplications(registry);
    registry->RegisterApplication(
        "test",
        make_scoped_ptr<core::ApplicationLoader>(
            new TestApplicationLoader(test_)));
  }

  CoreAppTest* test_;
};

void CoreAppTest::WaitForAppExit() {
  base::RunLoop run_loop;
  quit_closure_ = run_loop.QuitClosure();
  run_loop.Run();
}

void CoreAppTest::Initialize(
    mojo::Shell* shell,
    const std::vector<std::string>& args) {
  shell_ = shell;
  args_ = args;
  RunApplication();
  quit_closure_.Run();
}

void CoreAppTest::AcceptConnection(
    const GURL& requestor_url,
    scoped_ptr<core::ApplicationConnection> connection,
    const GURL& resolved_url) {
}

void CoreAppTest::Quit() {}

void CoreAppTest::SetUp() {
  test_application_host_client_.reset(new TestCoreApplicationHostClient(this));
  core::CoreClient::SetApplicationHostClientForTest(
      test_application_host_client_.get());
  InProcessBrowserTest::SetUp();
}

// TODO(core): Build a better way (i.e., hook into the embedder's default root
// app once that's a thing) to bootstrap the test app launch. Then we can
// eliminate core::Shell::Get().
#define CORE_APP_TEST_F(test_fixture, test_name) \
    class test_fixture##_CoreAppTestWrapper : public test_fixture { \
     private: \
      void RunApplication() override; \
    }; \
    IN_PROC_BROWSER_TEST_( \
        test_fixture, test_name, test_fixture##_CoreAppTestWrapper, \
        ::testing::internal::GetTypeId<test_fixture##_CoreAppTestWrapper>()) { \
      core::Shell::Get()->Launch(GURL("system:test")); \
      WaitForAppExit(); \
    } \
    void test_fixture##_CoreAppTestWrapper::RunApplication()

// End of library code
////////////////////////////////////////////////////////////////////////////

class HostResolverRequestClient
    : public net::interfaces::HostResolverRequestClient {
 public:
  HostResolverRequestClient(
      mojo::InterfaceRequest<net::interfaces::HostResolverRequestClient>
          request,
      const base::Closure& completion_callback)
      : binding_(this, request.Pass()),
        completion_callback_(completion_callback) {}

  ~HostResolverRequestClient() override {}

  // net::interfaces::HostResolverRequestClient:
  void ReportResult(int32_t error,
                    net::interfaces::AddressListPtr addresses) override {
    completion_callback_.Run();

    // TODO(rockot): Maybe some real testing with a mock resolver.
    ASSERT_EQ(0, error);
  }

 private:
  mojo::Binding<net::interfaces::HostResolverRequestClient> binding_;
  base::Closure completion_callback_;
};

class NetServiceAppTest : public CoreAppTest {
 protected:
  net::interfaces::HostResolverRequestInfoPtr NewHostResolverRequest(
      const std::string& hostname) {
    net::interfaces::HostResolverRequestInfoPtr request =
        net::interfaces::HostResolverRequestInfo::New();
    request->host = mojo::String::From(hostname);
    request->port = 0;
    request->address_family = net::interfaces::ADDRESS_FAMILY_IPV4;
    request->is_my_ip_address = false;
    return request.Pass();
  }
};

CORE_APP_TEST_F(NetServiceAppTest, TestHostResolver) {
  scoped_ptr<core::ApplicationConnection> net =
      core::ApplicationConnection::Create(shell(), GURL("system:net"));
  net::interfaces::HostResolverPtr resolver =
      net->ConnectToService<net::interfaces::HostResolver>();

  base::RunLoop run_loop;
  net::interfaces::HostResolverRequestClientPtr client_proxy;
  scoped_ptr<HostResolverRequestClient> client(new HostResolverRequestClient(
      mojo::GetProxy(&client_proxy), run_loop.QuitClosure()));
  resolver->Resolve(NewHostResolverRequest("google.com"),
                    client_proxy.Pass());
  run_loop.Run();
}
