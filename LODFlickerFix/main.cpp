#include "nvse/PluginAPI.h"

static uint32_t uiSetRenderStateAddr = 0;
static const float fDepthBias = 0.00002f;
static constexpr uint32_t uiShaderLoaderVersion = 131;

// Hooks.
D3DCMPFUNC DepthCompareFuncMap[] = {
	D3DCMP_NEVER,			// 0
	D3DCMP_NEVER,			// 1
	D3DCMP_GREATER,			// 2 - D3DCMP_LESS -> D3DCMP_GREATER
	D3DCMP_EQUAL,			// 3
	D3DCMP_GREATEREQUAL,	// 4 - D3DCMP_LESSEQUAL -> D3DCMP_GREATEREQUAL
	D3DCMP_LESS,			// 5 - D3DCMP_GREATER -> D3DCMP_LESS
	D3DCMP_NOTEQUAL,		// 6
	D3DCMP_LESSEQUAL,		// 7 - D3DCMP_GREATEREQUAL -> D3DCMP_LESSEQUAL
	D3DCMP_ALWAYS			// 8
};

class NiDX9RenderState {
public:
	void SetRenderState(D3DRENDERSTATETYPE aeState, uint32_t auiValue, uint32_t, bool abSave) {
		// Swap the depth comparison direction.
		if (aeState == D3DRS_ZFUNC)
			auiValue = DepthCompareFuncMap[auiValue];

		// Call the original method after swapping auiValue.
		ThisCall(uiSetRenderStateAddr, this, aeState, auiValue, 0, abSave);
	}
};


static uint32_t uiOrthoReturnAddr = 0xE6CB0A;
void __declspec(naked) OrthoHook() {
	__asm {
		fchs							// fInv = -fInv (turn fInv into negative)
		fst     dword ptr[esi + 0x9E8]  // m_kD3DProj._33 = fInv
		fchs							// fInv = -fInv (turn fInv back into positive)
		fmul    dword ptr[edi + 0x14]	// fInv * kFrustum.m_fFar
		fstp    dword ptr[esi + 0x9F8]	// m_kD3DProj._43 = result of above
		jmp		uiOrthoReturnAddr
	}
}

void InitHooks(bool abGECK) {
	SafeWrite8((abGECK ? 0xC06383 : 0xE6B9B3) + 1, 0xEE); // Set Z clear value to 0
	SafeWrite32((abGECK ? 0x8F8892 : 0xB4F932) + 2, (uint32_t)&fDepthBias); // Set depth bias to 0.00002

	// NiDX9RenderState::SetSamplerState hook.
	// Handles the depth comparison function.
	uiSetRenderStateAddr = abGECK ? 0xC214B0 : 0xE88780;
	ReplaceVirtualFuncEx(abGECK ? 0xE0F85C : 0x10EF674, &NiDX9RenderState::SetRenderState);
	ReplaceVirtualFuncEx(abGECK ? 0xE10CA4 : 0x10F08F4, &NiDX9RenderState::SetRenderState);

	// NiDX9Renderer::Do_SetCameraData hook.
	// Replaces:
	// m_kD3DProj._33 = kFrustum.m_fFar * fInvFmN;
	// m_kD3DProj._43 = -(kFrustum.m_fNear * kFrustum.m_fFar * fInvFmN);
	// With:
	// m_kD3DProj._33 = -(kFrustum.m_fNear * fInvFmN);
	// m_kD3DProj._43 = kFrustum.m_fNear * kFrustum.m_fFar * fInvFmN;
	SafeWriteBuf(abGECK ? 0xC07587 : 0xE6CBB7, "\xD8\x4F\x10\xD9\xE0\xD9\x9E\xE8\x09\x00\x00\xD9\x47\x10\xD8\x4F\x14\xDE\xC9", 19);

	// Same thing, but for orthographic projection.
	WriteRelJump(abGECK ? 0xC074C9 : 0xE6CAF9, OrthoHook);
	if (abGECK)
		uiOrthoReturnAddr = 0xC074DA;
}


EXTERN_DLL_EXPORT bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "LOD Flicker Fix";
	info->version = 100;
	return true;
}

EXTERN_DLL_EXPORT bool NVSEPlugin_Load(NVSEInterface* nvse) {
	HMODULE hShaderLoader = GetModuleHandle("Fallout Shader Loader.dll");

	if (hShaderLoader) {
		auto pQuery = (_NVSEPlugin_Query)GetProcAddress(hShaderLoader, "NVSEPlugin_Query");
		PluginInfo kInfo = {};
		pQuery(nvse, &kInfo);
		if (kInfo.version < uiShaderLoaderVersion) {
			char cBuffer[192];
			sprintf_s(cBuffer, "Fallout Shader Loader is outdated.\nPlease update it to use LOD Flicker Fix!\nCurrent version: %i\nMinimum required version: %i", kInfo.version, uiShaderLoaderVersion);
			MessageBox(NULL, cBuffer, "LOD Flicker Fix", MB_OK | MB_ICONERROR);
			ExitProcess(0);
		}
		else {
			InitHooks(nvse->isEditor);
		}
	}
	else {
		MessageBox(NULL, "Fallout Shader Loader not found.\nLOD Flicker Fix cannot be used without it, please install it.", "LOD Flicker Fix", MB_OK | MB_ICONERROR);
		ExitProcess(0);
	}

	return true;
}

BOOL WINAPI DllMain(
	HANDLE  hDllHandle,
	DWORD   dwReason,
	LPVOID  lpreserved
)
{
	return TRUE;
}