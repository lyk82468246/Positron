#include "positron_media.h"

#include <dshow.h>

extern "C" int pmedia_system_probe(void)
{
    IGraphBuilder *graph;
    HRESULT init_result;
    HRESULT graph_result;
    int should_uninitialize;

    graph = NULL;
    should_uninitialize = 0;
    init_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(init_result)) {
        should_uninitialize = 1;
    } else if (init_result != RPC_E_CHANGED_MODE) {
        return 0;
    }
    graph_result = CoCreateInstance(CLSID_FilterGraphNoThread,
                                    NULL,
                                    CLSCTX_INPROC_SERVER,
                                    IID_IGraphBuilder,
                                    (void **)&graph);
    if (SUCCEEDED(graph_result) && graph != NULL) {
        graph->Release();
        if (should_uninitialize) {
            CoUninitialize();
        }
        return 1;
    }
    if (should_uninitialize) {
        CoUninitialize();
    }
    return 0;
}
