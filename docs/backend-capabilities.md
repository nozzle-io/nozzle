# Backend capabilities

This matrix is the first-slice public contract for backend capability queries.
It is intentionally conservative: operation-specific format columns must not be
read as a generic “supported formats” list.

## Static capabilities vs availability

`get_backend_capabilities(...)` / `nozzle_get_backend_capabilities(...)` return
the static reference contract for a backend implementation. They do not mean
that the backend is compiled into the current library, selectable on the current
platform, or usable by the current process/device.

Use `is_backend_available(...)` / `nozzle_backend_is_available(...)` to answer
only the current-build question: “does this compiled nozzle library contain this
backend?”. A nonzero result is not a runtime/device probe. Backends marked with
`may_require_runtime_probe` can still fail when the required API, device,
context, permissions, or external resource is not available.

OpenGL is represented as an interop/native-texture publish path with runtime
context requirements. It is not advertised as a normal sender/receiver backend
in the static capability flags.

The C ABI v1 `NozzleBackendCapabilities` policy is exact-size: callers may pass
`struct_size == 0` for a zero-initialized output struct, or
`struct_size == sizeof(NozzleBackendCapabilities)`. Any other nonzero size is
invalid. Future extensions must bump `NOZZLE_BACKEND_CAPABILITIES_VERSION` or
add a new struct/API instead of silently changing this v1 layout.

- `requested_formats` are semantic formats accepted by the high-level API.
- `writable_storage_formats` are actual storage formats for writable frames.
- `native_publish_formats` are formats usable through native texture copy/blit
  publish paths.
- `direct_publish_formats` are zero-copy external publish paths such as
  IOSurface or DMA-BUF direct publish.
- `cpu_read_formats` / `cpu_write_formats` describe CPU mapping/copy paths for
  actual frame storage.

`rgb*` semantic formats may appear in `requested_formats` without appearing in
storage columns. That distinction is intentional and prevents bindings from
assuming that semantic RGB is a native storage format on backends that expand it
to RGBA/BGRA storage.

The table between the markers is checked by `nozzle_tests` against the compiled
capability table.

Decoded fixed values used by the table:

- `capability_flags`: `sender=1`, `receiver=2`, `writable_frames=4`,
  `native_texture_publish=8`, `direct_external_publish=16`, `cpu_read=32`,
  `cpu_write=64`, `zero_copy_receive=128`, `zero_copy_publish=256`,
  `requires_matching_backend=512`, `may_require_runtime_probe=2048`,
  `single_sender_per_process=1024`.
- `sharing_mechanisms`: `iosurface=1`, `d3d11_nt_handle=2`,
  `dma_buf=4`, `opengl_texture=8`.
- `native_kind`: `mtl_pixel_format=1`, `dxgi_format=2`, `drm_fourcc=3`,
  `gl_internal_format=4`.

<!-- NOZZLE_BACKEND_CAPABILITIES_TABLE:BEGIN -->
| backend | capability_flags | sharing_mechanisms | native_kind | max_senders_per_process | requested_formats | writable_storage_formats | native_publish_formats | direct_publish_formats | cpu_read_formats | cpu_write_formats |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |
| d3d11 | 1007 | 2 | 2 | 0 | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, rgba8_srgb, bgra8_srgb, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r16_float, rg16_float, rgb16_float, rgba16_float, r32_float, rg32_float, rgb32_float, rgba32_float, r32_uint, rgba32_uint, rgb32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, rgba8_srgb, bgra8_srgb, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, rgba8_srgb, bgra8_srgb, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint | none | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, rgba8_srgb, bgra8_srgb, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, rgba8_srgb, bgra8_srgb, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint |
| metal | 1023 | 1 | 1 | 0 | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r16_float, rg16_float, rgb16_float, rgba16_float, r32_float, rg32_float, rgb32_float, rgba32_float, r32_uint, rgba32_uint, rgb32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint | r8_unorm, rg8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgba16_unorm, r16_float, rg16_float, rgba16_float, r32_float, rg32_float, rgba32_float, r32_uint, rgba32_uint |
| dma_buf | 3447 | 4 | 3 | 1 | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, rgba8_srgb, bgra8_srgb, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r32_uint, rgba32_uint, rgb32_uint | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r32_uint, rgba32_uint, rgb32_uint | none | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r32_uint, rgba32_uint, rgb32_uint | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r32_uint, rgba32_uint, rgb32_uint | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r32_uint, rgba32_uint, rgb32_uint |
| opengl | 2056 | 8 | 4 | 0 | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r16_float, rg16_float, rgb16_float, rgba16_float, r32_float, rg32_float, rgb32_float, rgba32_float, r32_uint, rgba32_uint, rgb32_uint | none | r8_unorm, rg8_unorm, rgb8_unorm, rgba8_unorm, bgra8_unorm, r16_unorm, rg16_unorm, rgb16_unorm, rgba16_unorm, r16_float, rg16_float, rgb16_float, rgba16_float, r32_float, rg32_float, rgb32_float, rgba32_float, r32_uint, rgba32_uint, rgb32_uint | none | none | none |
<!-- NOZZLE_BACKEND_CAPABILITIES_TABLE:END -->

## Notes

- `max_senders_per_process == 0` means no static limit is published by this
  table. Runtime resource exhaustion can still fail individual operations.
- DMA-BUF float formats are intentionally absent from support and quality-loss
  capability bits until a float-preserving FourCC/native format model and
  GBM/EGL capability path are implemented.

- Metal sRGB formats are intentionally absent from requested/storage/native/direct/CPU
  columns because IOSurface-backed texture creation currently rejects them.
- OpenGL sRGB publish is intentionally absent from the static contract because
  the macOS IOSurface publish path rejects it.
