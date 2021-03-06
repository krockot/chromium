// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/chromeos/settings/stub_cros_settings_provider.h"
#include "chrome/browser/extensions/updater/chromeos_extension_cache_delegate.h"
#include "chrome/browser/extensions/updater/extension_cache_impl.h"
#include "chrome/browser/extensions/updater/local_extension_cache.h"
#include "chromeos/settings/cros_settings_names.h"
#include "chromeos/settings/cros_settings_provider.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {
namespace {

const char kTestExtensionId1[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char kTestExtensionId2[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char kTestExtensionId3[] = "cccccccccccccccccccccccccccccccc";

// 2 megabytes are a little less than 2 100 000 bytes, so the third extension
// will not be kept.
const size_t kMaxCacheSize = 2 * 1024 * 1024;
const size_t kExtensionSize1 = 1000 * 1000;
const size_t kExtensionSize2 = 1000 * 1000;
const size_t kExtensionSize3 = 100 * 1000;

static void CreateFile(const base::FilePath& file,
                       size_t size,
                       const base::Time& timestamp) {
  const std::string data(size, 0);
  EXPECT_EQ(base::WriteFile(file, data.data(), data.size()),
            static_cast<int>(size));
  EXPECT_TRUE(base::TouchFile(file, timestamp, timestamp));
}

static base::FilePath CreateExtensionFile(const base::FilePath& dir,
                                          const std::string& id,
                                          const std::string& version,
                                          size_t size,
                                          const base::Time& timestamp) {
  const base::FilePath filename = dir.Append(
      LocalExtensionCache::ExtensionFileName(id, version, "" /* hash */));
  CreateFile(filename, size, timestamp);
  return filename;
}

}  // namespace

class ExtensionCacheTest : public testing::Test {
 public:
  void SetUp() override {
    // Swap out the DeviceSettingsProvider with our stub settings provider
    // so we can set values for maximum extension cache size.
    chromeos::CrosSettings* const cros_settings = chromeos::CrosSettings::Get();
    device_settings_provider_ =
        cros_settings->GetProvider(chromeos::kExtensionCacheSize);
    EXPECT_TRUE(device_settings_provider_);
    EXPECT_TRUE(
        cros_settings->RemoveSettingsProvider(device_settings_provider_));
    cros_settings->AddSettingsProvider(&stub_settings_provider_);
  }

  void TearDown() override {
    // Restore the real DeviceSettingsProvider.
    chromeos::CrosSettings* const cros_settings = chromeos::CrosSettings::Get();
    EXPECT_TRUE(
        cros_settings->RemoveSettingsProvider(&stub_settings_provider_));
    cros_settings->AddSettingsProvider(device_settings_provider_);
  }

  // Helpers used to mock out cros settings.
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;
  chromeos::CrosSettingsProvider* device_settings_provider_ = nullptr;
  chromeos::StubCrosSettingsProvider stub_settings_provider_;

  content::TestBrowserThreadBundle thread_bundle_;
};

TEST_F(ExtensionCacheTest, SizePolicy) {
  chromeos::CrosSettings::Get()->SetInteger(chromeos::kExtensionCacheSize,
                                            kMaxCacheSize);

  // Create and initialize local cache.
  const base::Time now = base::Time::Now();
  base::ScopedTempDir cache_dir;
  ASSERT_TRUE(cache_dir.CreateUniqueTempDir());
  const base::FilePath cache_path = cache_dir.path();
  CreateFile(cache_path.Append(LocalExtensionCache::kCacheReadyFlagFileName), 0,
             now);

  // The extension cache only has enough space for two of the three extensions.
  const base::FilePath file1 =
      CreateExtensionFile(cache_path, kTestExtensionId1, "1.0", kExtensionSize1,
                          now - base::TimeDelta::FromSeconds(1));
  const base::FilePath file2 =
      CreateExtensionFile(cache_path, kTestExtensionId2, "2.0", kExtensionSize2,
                          now - base::TimeDelta::FromSeconds(2));
  const base::FilePath file3 =
      CreateExtensionFile(cache_path, kTestExtensionId3, "3.0", kExtensionSize3,
                          now - base::TimeDelta::FromSeconds(3));

  ExtensionCacheImpl cache_impl(
      make_scoped_ptr(new ChromeOSExtensionCacheDelegate(cache_path)));

  scoped_ptr<base::RunLoop> run_loop(new base::RunLoop);
  cache_impl.Start(run_loop->QuitClosure());
  run_loop->Run();

  run_loop.reset(new base::RunLoop);
  cache_impl.Shutdown(run_loop->QuitClosure());
  run_loop->Run();

  EXPECT_TRUE(base::PathExists(file1));
  EXPECT_TRUE(base::PathExists(file2));
  EXPECT_FALSE(base::PathExists(file3));
}

}  // namespace extensions
