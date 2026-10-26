#include <metahook.h>
#include <IStandaloneView.h>
#include <cstdio>

namespace
{
class InactiveView : public IStandaloneView
{
public:
    bool IsActive() const override { return false; }
    void CalcRefdef(ref_params_s*) override {}
    void OnBeforeRenderView() override {}
    void OnAfterRenderView() override {}
    void DrawNormalTriangles(IStandaloneViewDrawfuncs*) override {}
    void DrawTransparentTriangles(IStandaloneViewDrawfuncs*) override {}
};
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    const auto module = LoadLibraryA(argv[1]);
    if (!module)
    {
        std::printf("FAIL loading StandaloneView: %lu\n", GetLastError());
        return 1;
    }
    const auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
    if (!factory)
    {
        std::printf("FAIL missing CreateInterface export\n");
        FreeLibrary(module);
        return 1;
    }
    int        code002     = IFACE_FAILED;
    int        code003     = IFACE_FAILED;
    int        code001     = IFACE_OK;
    int        codeUnknown = IFACE_OK;
    auto*      api002      = static_cast<IStandaloneViewManager*>(factory(STANDALONEVIEW_MANAGER_INTERFACE_VERSION, &code002));
    auto*      api003      = static_cast<IStandaloneViewManager*>(factory(STANDALONEVIEW_MANAGER_INTERFACE_VERSION_V003, &code003));
    auto*      api001      = factory("StandaloneViewManager_API_001", &code001);
    auto*      unknown     = factory("StandaloneViewManager_API_999", &codeUnknown);
    const bool success     = api002 && api002 == api003 && code002 == IFACE_OK && code003 == IFACE_OK &&
        !api001 && code001 == IFACE_FAILED && !unknown && codeUnknown == IFACE_FAILED;
    if (success)
    {
        InactiveView view;
        // Exercise the unchanged manager vtable across the actual DLL boundary.
        api002->RegisterStandaloneView(&view);
        api003->RegisterStandaloneView(&view);
        api003->UnregisterStandaloneView(&view);
        api002->UnregisterStandaloneView(&view);
    }
    std::printf("StandaloneView interface: %s\n", success ? "passed" : "FAILED");
    FreeLibrary(module);
    return success ? 0 : 1;
}
