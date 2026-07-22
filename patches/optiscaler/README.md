# OptiScaler RayTracedGL1 integration

OptiScaler v0.9.4 injects into Xash through its `dbghelp.dll` proxy, but its
Vulkan FSR2 input hook looks for `ffxFsr2*` exports only in `xash3d.exe`.
RayTracedGL1 statically links and exports those functions from
`RayTracedGL1.dll`, so the stock lookup returns zero addresses.

Apply `0001-Hook-FSR2-exports-from-a-caller-supplied-module.patch` to the
official v0.9.4 source revision:

- Repository: `https://github.com/optiscaler/OptiScaler`
- Base revision: `7534ad00bf9e590eedb99e8dd9fd8c89dae3654f`
- Configuration: `Release|x64`

The patch keeps the existing executable lookup for normal users and adds the
exported `OptiScalerHookFSR2VkModule` handshake used by the matching
RayTracedGL1 dependency patch. It excludes the optional `ffxGetDeviceVK`
detour because optimized static FSR2 builds can fold that identity function
with `ffxGetCommandListVK`; hooking the shared address corrupts the saved
Vulkan device on the first command-list conversion.

The active `dbghelp.dll` is therefore a locally built, unsigned OptiScaler
binary rather than the stock signed proxy. Its tested SHA-256 is
`1819121CDFF8D1155C0B61C44AFE0E39F8935121D62AC86135AF8D1798D47B17`.
The module lookup handshake is the only local OptiScaler behavior change;
Vulkan-to-DX12 interop, resource conversion, the overlay, and FSR output are
otherwise the normal v0.9.4 implementation.

Merge the values in `StillHunger-settings.ini` into the official v0.9.4 INI.
The verified RX 9070 XT route uses RayTracedGL1's native Vulkan FSR2 input and
OptiScaler's `fsr31_12` output. The bundled
`amd_fidelityfx_upscaler_dx12.dll` is already FSR 4.1.1, so it runs FSR4
directly even with the separate legacy-module upgrade path disabled. File
logging and the FSR4 watermark are disabled after validation.
