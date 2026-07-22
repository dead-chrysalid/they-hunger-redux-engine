# OptiScaler RayTracedGL1 module-hook patch

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
Vulkan device on the first command-list conversion. The isolated runtime
preserves the official signed proxy as `dbghelp.optiscaler-official.dll`;
locally patched builds are unsigned.
