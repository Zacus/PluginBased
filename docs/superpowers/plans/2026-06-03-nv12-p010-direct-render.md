# NV12/P010 Direct Render Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add CPU-frame direct rendering for `AV_PIX_FMT_NV12` and `AV_PIX_FMT_P010LE` in PlayPlugin without converting them to `YUV420P`.

**Architecture:** Keep the current FFmpeg decode queue and QSGMaterial pipeline. Extend decoder supported formats, extend `FFmpegSurface` format metadata for semiplanar UV textures, and keep one shader that selects planar or semiplanar sampling from a compact format mode uniform.

**Tech Stack:** C++17, Qt 6 Scene Graph / QRhi, QML shader pipeline, FFmpeg pixel formats, Python regression script.

---

## File Structure

- Modify `tests/playplugin_regression_checks.py`: add static regression checks for NV12/P010 direct render behavior.
- Modify `plugins/PlayPlugin/src/FFmpegDecoder.cpp`: allow `AV_PIX_FMT_NV12` and `AV_PIX_FMT_P010LE` through without `sws_scale`.
- Modify `plugins/PlayPlugin/src/FFmpegSurface.cpp`: add planar/semiplanar texture metadata, use `RG8`/`RG16` UV textures, and upload two-plane frames correctly.
- Modify `plugins/PlayPlugin/shaders/yuvvideo.frag`: sample NV12/P010 from `texU.rg` and preserve existing planar behavior.

### Task 1: Regression Checks

**Files:**
- Modify: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Write failing checks**

Add checks that require decoder, surface, and shader support for semiplanar formats:

```python
shader_frag = read("plugins/PlayPlugin/shaders/yuvvideo.frag")
require("AV_PIX_FMT_NV12" in decoder_cpp and "AV_PIX_FMT_P010LE" in decoder_cpp,
        "decoder should let NV12 and P010 frames bypass sws normalization")
require("PlaneLayout" in surface_cpp and "Semiplanar" in surface_cpp,
        "FFmpegSurface should distinguish planar and semiplanar YUV layouts")
require("QRhiTexture::RG8" in surface_cpp and "QRhiTexture::RG16" in surface_cpp,
        "NV12/P010 UV planes should upload as two-channel RHI textures")
require("formatMode" in surface_cpp,
        "FFmpegSurface should pass a compact shader format mode")
require(".rg" in shader_frag and "semiplanar" in shader_frag,
        "shader should sample NV12/P010 UV from texU.rg")
```

- [ ] **Step 2: Run regression script to verify failure**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL with at least one message about missing NV12/P010 direct render support.

### Task 2: Decoder Format Bypass

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegDecoder.cpp`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Add NV12/P010 to renderer-supported formats**

In `isRendererSupportedVideoFormat()`, add:

```cpp
case AV_PIX_FMT_NV12:
case AV_PIX_FMT_P010LE:
    return true;
```

- [ ] **Step 2: Run regression script**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: still FAIL until Surface and shader support are implemented.

### Task 3: Surface Semiplanar Texture Upload

**Files:**
- Modify: `plugins/PlayPlugin/src/FFmpegSurface.cpp`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Extend format metadata**

Replace the current `PixelFormatInfo` fields with a layout-aware form:

```cpp
enum class PlaneLayout { Planar, Semiplanar };

struct PixelFormatInfo
{
    bool valid = false;
    PlaneLayout planeLayout = PlaneLayout::Planar;
    QRhiTexture::Format lumaFormat = QRhiTexture::R8;
    QRhiTexture::Format chromaFormat = QRhiTexture::R8;
    int chromaWidthDivisor = 2;
    int chromaHeightDivisor = 2;
bool is10bit = false;
bool needs10BitExpansion = false;
float formatMode = 0.0f;
    int chromaWidth(int w) const { return (w + chromaWidthDivisor - 1) / chromaWidthDivisor; }
    int chromaHeight(int h) const { return (h + chromaHeightDivisor - 1) / chromaHeightDivisor; }
    bool isSemiplanar() const { return planeLayout == PlaneLayout::Semiplanar; }
};
```

- [ ] **Step 2: Map pixel formats**

Set:

```cpp
AV_PIX_FMT_YUV420P/YUVJ420P -> Planar, R8, R8, no expansion, mode 0
AV_PIX_FMT_YUV420P10LE/YUV422P10LE/YUV444P10LE -> Planar, R16, R16, low-10bit expansion, mode 1
AV_PIX_FMT_NV12 -> Semiplanar, R8, RG8, no expansion, mode 2
AV_PIX_FMT_P010LE -> Semiplanar, R16, RG16, no expansion because P010 stores values in the high 10 bits, mode 3
```

- [ ] **Step 3: Upload black placeholder by layout**

For semiplanar formats, upload Y black to `tex_y`, upload interleaved UV neutral data to `tex_u`, and do not upload `tex_v` unless it exists as a placeholder. For planar formats, keep current Y/U/V behavior.

- [ ] **Step 4: Upload pending frames by layout**

For semiplanar formats, upload:

```cpp
upload_plane(mat->tex_y, frame->data[0], frame->linesize[0], { frame->width, frame->height });
upload_plane(mat->tex_u, frame->data[1], frame->linesize[1], { cw, ch });
```

For planar formats, keep the existing three upload calls.

- [ ] **Step 5: Run regression script**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: still FAIL until shader support is implemented.

### Task 4: Shader Semiplanar Sampling

**Files:**
- Modify: `plugins/PlayPlugin/shaders/yuvvideo.frag`
- Test: `tests/playplugin_regression_checks.py`

- [ ] **Step 1: Decode format mode**

In `main()`, compute:

```glsl
float formatMode = ubuf.params.w;
bool needs10bitExpansion = formatMode == 1.0;
bool semiplanar = formatMode >= 2.0;
```

- [ ] **Step 2: Sample UV by layout**

Use:

```glsl
float y = samplePlane(texY, vTexCoord);
float u = 0.0;
float v = 0.0;
if (semiplanar) {
    vec2 uv = texture(texU, vTexCoord).rg;
    u = uv.r;
    v = uv.g;
} else {
    u = samplePlane(texU, vTexCoord);
    v = samplePlane(texV, vTexCoord);
}
```

- [ ] **Step 3: Preserve 10bit scaling and color matrix**

Use `if (needs10bitExpansion)` for the existing planar 10bit scaling block. Do not scale P010 again because its samples are already high-bit aligned in R16/RG16 textures. Leave full-range and BT.709/BT.601 conversion logic unchanged.

- [ ] **Step 4: Run regression script**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS.

### Task 5: Build Verification

**Files:**
- Verify: full project build

- [ ] **Step 1: Run CMake build**

Run: `cmake --build build --parallel`

Expected: exit code 0. If shader compilation fails, fix GLSL compatibility and rerun.

- [ ] **Step 2: Report verification**

Report the exact commands run and whether they passed. If build cannot run due missing local dependencies or stale build directory, report that blocker with the command output.
