// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/gpu/compositor_util.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_number_conversions.h"
#include "base/sys_info.h"
#include "build/build_config.h"
#include "cc/base/switches.h"
#include "content/browser/gpu/gpu_data_manager_impl.h"
#include "content/public/common/content_switches.h"
#include "gpu/config/gpu_feature_type.h"

namespace content {

namespace {

static bool IsGpuRasterizationBlacklisted() {
  GpuDataManagerImpl* manager = GpuDataManagerImpl::GetInstance();
  return manager->IsFeatureBlacklisted(
        gpu::GPU_FEATURE_TYPE_GPU_RASTERIZATION);
}

const char* kGpuCompositingFeatureName = "gpu_compositing";
const char* kWebGLFeatureName = "webgl";
const char* kRasterizationFeatureName = "rasterization";
const char* kThreadedRasterizationFeatureName = "threaded_rasterization";
const char* kMultipleRasterThreadsFeatureName = "multiple_raster_threads";

const int kMinRasterThreads = 1;
const int kMaxRasterThreads = 64;

const int kMinMSAASampleCount = 0;

struct GpuFeatureInfo {
  std::string name;
  bool blocked;
  bool disabled;
  std::string disabled_description;
  bool fallback_to_software;
};

const GpuFeatureInfo GetGpuFeatureInfo(size_t index, bool* eof) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  GpuDataManagerImpl* manager = GpuDataManagerImpl::GetInstance();

  const GpuFeatureInfo kGpuFeatureInfo[] = {
      {
          "2d_canvas",
          manager->IsFeatureBlacklisted(
              gpu::GPU_FEATURE_TYPE_ACCELERATED_2D_CANVAS),
          command_line.HasSwitch(switches::kDisableAccelerated2dCanvas) ||
          !GpuDataManagerImpl::GetInstance()->
              GetGPUInfo().SupportsAccelerated2dCanvas(),
          "Accelerated 2D canvas is unavailable: either disabled at the command"
          " line or not supported by the current system.",
          true
      },
      {
          kGpuCompositingFeatureName,
          manager->IsFeatureBlacklisted(gpu::GPU_FEATURE_TYPE_GPU_COMPOSITING),
          command_line.HasSwitch(switches::kDisableGpuCompositing),
          "Gpu compositing has been disabled, either via about:flags or"
          " command line. The browser will fall back to software compositing"
          " and hardware acceleration will be unavailable.",
          true
      },
      {
          kWebGLFeatureName,
          manager->IsFeatureBlacklisted(gpu::GPU_FEATURE_TYPE_WEBGL),
          command_line.HasSwitch(switches::kDisableExperimentalWebGL),
          "WebGL has been disabled, either via about:flags or command line.",
          false
      },
      {
          "flash_3d",
          manager->IsFeatureBlacklisted(gpu::GPU_FEATURE_TYPE_FLASH3D),
          command_line.HasSwitch(switches::kDisableFlash3d),
          "Using 3d in flash has been disabled, either via about:flags or"
          " command line.",
          true
      },
      {
          "flash_stage3d",
          manager->IsFeatureBlacklisted(gpu::GPU_FEATURE_TYPE_FLASH_STAGE3D),
          command_line.HasSwitch(switches::kDisableFlashStage3d),
          "Using Stage3d in Flash has been disabled, either via about:flags or"
          " command line.",
          true
      },
      {
          "flash_stage3d_baseline",
          manager->IsFeatureBlacklisted(
              gpu::GPU_FEATURE_TYPE_FLASH_STAGE3D_BASELINE) ||
          manager->IsFeatureBlacklisted(gpu::GPU_FEATURE_TYPE_FLASH_STAGE3D),
          command_line.HasSwitch(switches::kDisableFlashStage3d),
          "Using Stage3d Baseline profile in Flash has been disabled, either"
          " via about:flags or command line.",
          true
      },
      {
          "video_decode",
          manager->IsFeatureBlacklisted(
              gpu::GPU_FEATURE_TYPE_ACCELERATED_VIDEO_DECODE),
          command_line.HasSwitch(switches::kDisableAcceleratedVideoDecode),
          "Accelerated video decode has been disabled, either via about:flags"
          " or command line.",
          true
      },
#if defined(ENABLE_WEBRTC)
      {
          "video_encode",
          manager->IsFeatureBlacklisted(
              gpu::GPU_FEATURE_TYPE_ACCELERATED_VIDEO_ENCODE),
          command_line.HasSwitch(switches::kDisableWebRtcHWEncoding),
          "Accelerated video encode has been disabled, either via about:flags"
          " or command line.",
          true
      },
#endif
#if defined(OS_CHROMEOS)
      {
          "panel_fitting",
          manager->IsFeatureBlacklisted(gpu::GPU_FEATURE_TYPE_PANEL_FITTING),
          command_line.HasSwitch(switches::kDisablePanelFitting),
          "Panel fitting has been disabled, either via about:flags or command"
          " line.",
          false
      },
#endif
      {
          kRasterizationFeatureName,
          IsGpuRasterizationBlacklisted() &&
          !IsGpuRasterizationEnabled() && !IsForceGpuRasterizationEnabled(),
          !IsGpuRasterizationEnabled() && !IsForceGpuRasterizationEnabled() &&
          !IsGpuRasterizationBlacklisted(),
          "Accelerated rasterization has been disabled, either via about:flags"
          " or command line.",
          true
      },
      {
          kThreadedRasterizationFeatureName,
          false,
          !IsImplSidePaintingEnabled(),
          "Threaded rasterization has not been enabled or"
          " is not supported by the current system.",
          false
      },
      {
          kMultipleRasterThreadsFeatureName,
          false,
          NumberOfRendererRasterThreads() == 1,
          "Raster is using a single thread.",
          false
      },
  };
  DCHECK(index < arraysize(kGpuFeatureInfo));
  *eof = (index == arraysize(kGpuFeatureInfo) - 1);
  return kGpuFeatureInfo[index];
}

}  // namespace

bool IsPinchVirtualViewportEnabled() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  // Command line switches take precedence over platform default.
  if (command_line.HasSwitch(cc::switches::kDisablePinchVirtualViewport))
    return false;
  if (command_line.HasSwitch(cc::switches::kEnablePinchVirtualViewport))
    return true;

  return true;
}

bool IsPropertyTreeVerificationEnabled() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  return command_line.HasSwitch(cc::switches::kEnablePropertyTreeVerification);
}

bool IsDelegatedRendererEnabled() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  bool enabled = false;

#if defined(USE_AURA) || defined(OS_MACOSX)
  // Enable on Aura and Mac.
  enabled = true;
#endif

  // Flags override.
  enabled |= command_line.HasSwitch(switches::kEnableDelegatedRenderer);
  enabled &= !command_line.HasSwitch(switches::kDisableDelegatedRenderer);
  return enabled;
}

bool IsImplSidePaintingEnabled() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kDisableImplSidePainting))
    return false;
  return true;
}

int NumberOfRendererRasterThreads() {
  int num_raster_threads = 1;

  // Async uploads uses its own thread, so allow an extra thread when async
  // uploads is not in use.
  bool allow_extra_thread =
      IsZeroCopyUploadEnabled() || IsOneCopyUploadEnabled();
  if (base::SysInfo::NumberOfProcessors() >= 4 && allow_extra_thread)
    num_raster_threads = 2;

  int force_num_raster_threads = ForceNumberOfRendererRasterThreads();
  if (force_num_raster_threads)
    num_raster_threads = force_num_raster_threads;

  return num_raster_threads;
}

bool IsOneCopyUploadEnabled() {
  if (IsZeroCopyUploadEnabled())
    return false;

  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kEnableOneCopy))
    return true;
  if (command_line.HasSwitch(switches::kDisableOneCopy))
    return false;

#if defined(OS_ANDROID)
  return false;
#endif
  return true;
}

bool IsZeroCopyUploadEnabled() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  return command_line.HasSwitch(switches::kEnableZeroCopy);
}

int ForceNumberOfRendererRasterThreads() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  if (!command_line.HasSwitch(switches::kNumRasterThreads))
    return 0;
  std::string string_value =
      command_line.GetSwitchValueASCII(switches::kNumRasterThreads);
  int force_num_raster_threads = 0;
  if (base::StringToInt(string_value, &force_num_raster_threads) &&
      force_num_raster_threads >= kMinRasterThreads &&
      force_num_raster_threads <= kMaxRasterThreads) {
    return force_num_raster_threads;
  } else {
    DLOG(WARNING) << "Failed to parse switch " <<
        switches::kNumRasterThreads  << ": " << string_value;
    return 0;
  }
}

bool IsGpuRasterizationEnabled() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  if (!IsImplSidePaintingEnabled())
    return false;

  if (command_line.HasSwitch(switches::kDisableGpuRasterization))
    return false;
  else if (command_line.HasSwitch(switches::kEnableGpuRasterization))
    return true;

  if (IsGpuRasterizationBlacklisted()) {
    return false;
  }

  return true;
}

bool IsForceGpuRasterizationEnabled() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  if (!IsImplSidePaintingEnabled())
    return false;

  return command_line.HasSwitch(switches::kForceGpuRasterization);
}

bool UseSurfacesEnabled() {
#if defined(OS_ANDROID)
  return false;
#endif
  bool enabled = false;
#if defined(USE_AURA) || defined(OS_MACOSX)
  enabled = true;
#endif

  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  // Flags override.
  enabled |= command_line.HasSwitch(switches::kUseSurfaces);
  enabled &= !command_line.HasSwitch(switches::kDisableSurfaces);
  return enabled;
}

int GpuRasterizationMSAASampleCount() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  if (!command_line.HasSwitch(switches::kGpuRasterizationMSAASampleCount))
    return 0;
  std::string string_value = command_line.GetSwitchValueASCII(
      switches::kGpuRasterizationMSAASampleCount);
  int msaa_sample_count = 0;
  if (base::StringToInt(string_value, &msaa_sample_count) &&
      msaa_sample_count >= kMinMSAASampleCount) {
    return msaa_sample_count;
  } else {
    DLOG(WARNING) << "Failed to parse switch "
                  << switches::kGpuRasterizationMSAASampleCount << ": "
                  << string_value;
    return 0;
  }
}

base::DictionaryValue* GetFeatureStatus() {
  GpuDataManagerImpl* manager = GpuDataManagerImpl::GetInstance();
  std::string gpu_access_blocked_reason;
  bool gpu_access_blocked =
      !manager->GpuAccessAllowed(&gpu_access_blocked_reason);

  base::DictionaryValue* feature_status_dict = new base::DictionaryValue();

  bool eof = false;
  for (size_t i = 0; !eof; ++i) {
    const GpuFeatureInfo gpu_feature_info = GetGpuFeatureInfo(i, &eof);
    std::string status;
    if (gpu_feature_info.disabled) {
      status = "disabled";
      if (gpu_feature_info.fallback_to_software)
        status += "_software";
      else
        status += "_off";
      if (gpu_feature_info.name == kThreadedRasterizationFeatureName)
        status += "_ok";
    } else if (gpu_feature_info.blocked ||
               gpu_access_blocked) {
      status = "unavailable";
      if (gpu_feature_info.fallback_to_software)
        status += "_software";
      else
        status += "_off";
    } else {
      status = "enabled";
      if (gpu_feature_info.name == kWebGLFeatureName &&
          manager->IsFeatureBlacklisted(gpu::GPU_FEATURE_TYPE_GPU_COMPOSITING))
        status += "_readback";
      if (gpu_feature_info.name == kRasterizationFeatureName) {
        if (IsForceGpuRasterizationEnabled())
          status += "_force";
      }
      if (gpu_feature_info.name == kMultipleRasterThreadsFeatureName) {
        if (ForceNumberOfRendererRasterThreads() > 0)
          status += "_force";
      }
      if (gpu_feature_info.name == kThreadedRasterizationFeatureName ||
          gpu_feature_info.name == kMultipleRasterThreadsFeatureName)
        status += "_on";
    }
    if (gpu_feature_info.name == kWebGLFeatureName &&
        (gpu_feature_info.blocked || gpu_access_blocked) &&
        manager->ShouldUseSwiftShader()) {
      status = "unavailable_software";
    }

    feature_status_dict->SetString(
        gpu_feature_info.name.c_str(), status.c_str());
  }
  return feature_status_dict;
}

base::Value* GetProblems() {
  GpuDataManagerImpl* manager = GpuDataManagerImpl::GetInstance();
  std::string gpu_access_blocked_reason;
  bool gpu_access_blocked =
      !manager->GpuAccessAllowed(&gpu_access_blocked_reason);

  base::ListValue* problem_list = new base::ListValue();
  manager->GetBlacklistReasons(problem_list);

  if (gpu_access_blocked) {
    base::DictionaryValue* problem = new base::DictionaryValue();
    problem->SetString("description",
        "GPU process was unable to boot: " + gpu_access_blocked_reason);
    problem->Set("crBugs", new base::ListValue());
    problem->Set("webkitBugs", new base::ListValue());
    base::ListValue* disabled_features = new base::ListValue();
    disabled_features->AppendString("all");
    problem->Set("affectedGpuSettings", disabled_features);
    problem->SetString("tag", "disabledFeatures");
    problem_list->Insert(0, problem);
  }

  bool eof = false;
  for (size_t i = 0; !eof; ++i) {
    const GpuFeatureInfo gpu_feature_info = GetGpuFeatureInfo(i, &eof);
    if (gpu_feature_info.disabled) {
      base::DictionaryValue* problem = new base::DictionaryValue();
      problem->SetString(
          "description", gpu_feature_info.disabled_description);
      problem->Set("crBugs", new base::ListValue());
      problem->Set("webkitBugs", new base::ListValue());
      base::ListValue* disabled_features = new base::ListValue();
      disabled_features->AppendString(gpu_feature_info.name);
      problem->Set("affectedGpuSettings", disabled_features);
      problem->SetString("tag", "disabledFeatures");
      problem_list->Append(problem);
    }
  }
  return problem_list;
}

std::vector<std::string> GetDriverBugWorkarounds() {
  return GpuDataManagerImpl::GetInstance()->GetDriverBugWorkarounds();
}

}  // namespace content
