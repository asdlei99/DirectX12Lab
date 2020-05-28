#include <thread>

#include <d3dx12.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>

#include <agz/d3d12/window.h>

AGZ_D3D12_LAB_BEGIN

namespace impl
{

    LRESULT CALLBACK windowMessageProc(HWND, UINT, WPARAM, LPARAM);

    std::unordered_map<HWND, Window*> &handleToWindow()
    {
        static std::unordered_map<HWND, Window *> ret;
        return ret;
    }

    Vec2i clientSizeToWindowSize(DWORD style, const Vec2i &clientSize)
    {
        RECT winRect = { 0, 0, clientSize.x, clientSize.y };
        if(!AdjustWindowRect(&winRect, style, FALSE))
            throw D3D12LabException("failed to compute window size");
        return { winRect.right - winRect.left, winRect.bottom - winRect.top };
    }

    constexpr DXGI_FORMAT SWAP_CHAIN_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

} // namespace  impl

struct WindowImplData
{
    // win32 window

    DWORD style = 0;

    std::wstring className;

    HWND hWindow        = nullptr;
    HINSTANCE hInstance = nullptr;

    int clientWidth  = 0;
    int clientHeight = 0;

    bool shouldClose = false;
    bool inFocus     = true;

    // d3d12

    ComPtr<ID3D12Device>       device;
    ComPtr<ID3D12CommandQueue> cmdQueue;

    ComPtr<IDXGISwapChain3> swapChain;
    int swapChainImageCount = 0;
    std::vector<ComPtr<ID3D12Resource>> swapChainBuffers;

    ComPtr<ID3D12DescriptorHeap> RTVDescHeap;
    UINT RTVDescSize = 0;

    ComPtr<ID3D12Fence> queueFence;
    UINT64 queueFenceValue = 0;
     
    // events

    std::unique_ptr<Keyboard> keyboard;
    std::unique_ptr<Mouse>    mouse;
};

DWORD WindowDesc::getStyle() const noexcept
{
    DWORD style = WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX;
    if(resizable)
        style |= WS_SIZEBOX | WS_MAXIMIZEBOX;
    return style;
}

void Window::initWin32Window(const WindowDesc &desc)
{
    impl_ = std::make_unique<WindowImplData>();

    // hInstance & class name

    impl_->hInstance = GetModuleHandle(nullptr);
    impl_->className = L"D3D12LabWindowClass" + std::to_wstring(
                                reinterpret_cast<size_t>(this));

    // register window class

    WNDCLASSEXW wc;
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = impl::windowMessageProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = impl_->hInstance;
    wc.hIcon         = nullptr;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = impl_->className.c_str();
    wc.hIconSm       = nullptr;

    if(!RegisterClassExW(&wc))
        throw D3D12LabException("failed to register window class");

    // screen info

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    RECT workAreaRect;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workAreaRect, 0);

    const int workAreaW = workAreaRect.right - workAreaRect.left;
    const int workAreaH = workAreaRect.bottom - workAreaRect.top;

    // window style & size

    const int clientW = desc.fullscreen ? screenW : desc.clientWidth;
    const int clientH = desc.fullscreen ? screenH : desc.clientHeight;

    impl_->style = desc.getStyle();

    const auto winSize = impl::clientSizeToWindowSize(
        impl_->style, { clientW, clientH });

    // window pos

    int left, top;
    if(desc.fullscreen)
    {
        left = (screenW - winSize.x) / 2;
        top  = (screenH - winSize.y) / 2;
    }
    else
    {
        left = workAreaRect.left + (workAreaW - winSize.x) / 2;
        top  = workAreaRect.top  + (workAreaH - winSize.y) / 2;
    }

    // create window

    impl_->hWindow = CreateWindowW(
        impl_->className.c_str(), desc.title.c_str(),
        impl_->style, left, top, winSize.x, winSize.y,
        nullptr, nullptr, impl_->hInstance, nullptr);
    if(!impl_->hWindow)
        throw D3D12LabException("failed to create win32 window");

    // show & focus

    ShowWindow(impl_->hWindow, SW_SHOW);
    UpdateWindow(impl_->hWindow);
    SetForegroundWindow(impl_->hWindow);
    SetFocus(impl_->hWindow);

    // client size

    RECT clientRect;
    GetClientRect(impl_->hWindow, &clientRect);
    impl_->clientWidth  = clientRect.right - clientRect.left;
    impl_->clientHeight = clientRect.bottom - clientRect.top;

    // event dispatcher

    impl::handleToWindow().insert({ impl_->hWindow, this });
}

void Window::initD3D12(const WindowDesc &desc)
{
    // factory

    ComPtr<IDXGIFactory4> dxgiFactory;
    AGZ_D3D12_CHECK_HR_MSG(
        "failed to create dxgi factory",
        CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf())));

    // device

    AGZ_D3D12_CHECK_HR_MSG(
        "failed to create d3d12 device",
        D3D12CreateDevice(
            nullptr, D3D_FEATURE_LEVEL_12_0,
            IID_PPV_ARGS(impl_->device.GetAddressOf())));
    
    // command queue

    D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
    AGZ_D3D12_CHECK_HR_MSG(
        "failed to create d3d12 command queue",
        impl_->device->CreateCommandQueue(
            &cmdQueueDesc, IID_PPV_ARGS(impl_->cmdQueue.GetAddressOf())));

    // swap chain

    DXGI_SWAP_CHAIN_DESC swapChainDesc;

    swapChainDesc.BufferDesc.Width                   = impl_->clientWidth;
    swapChainDesc.BufferDesc.Height                  = impl_->clientHeight;
    swapChainDesc.BufferDesc.Format                  = impl::SWAP_CHAIN_BUFFER_FORMAT;
    swapChainDesc.BufferDesc.RefreshRate.Numerator   = 0;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferDesc.Scaling                 = DXGI_MODE_SCALING_UNSPECIFIED;
    swapChainDesc.BufferDesc.ScanlineOrdering        = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    
    swapChainDesc.SampleDesc.Count   = desc.multisampleCount;
    swapChainDesc.SampleDesc.Quality = desc.multisampleQuality;

    swapChainDesc.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount  = desc.imageCount;
    swapChainDesc.OutputWindow = impl_->hWindow;
    swapChainDesc.Windowed     = TRUE;
    swapChainDesc.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags        = 0;

    IDXGISwapChain *tSwapChain;
    AGZ_D3D12_CHECK_HR_MSG(
        "failed to create dxgi swap chain",
        dxgiFactory->CreateSwapChain(
            impl_->cmdQueue.Get(), &swapChainDesc, &tSwapChain));
    impl_->swapChain.Attach(static_cast<IDXGISwapChain3 *>(tSwapChain));

    impl_->swapChainImageCount = desc.imageCount;

    if(desc.fullscreen)
        impl_->swapChain->SetFullscreenState(TRUE, nullptr);

    // render target descriptor heap

    D3D12_DESCRIPTOR_HEAP_DESC RTVDescHeapDesc;
    RTVDescHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    RTVDescHeapDesc.NodeMask       = 0;
    RTVDescHeapDesc.NumDescriptors = desc.imageCount;
    RTVDescHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    AGZ_D3D12_CHECK_HR_MSG(
        "failed to create d3d12 render target descriptor heap",
        impl_->device->CreateDescriptorHeap(
            &RTVDescHeapDesc, IID_PPV_ARGS(impl_->RTVDescHeap.GetAddressOf())));

    impl_->RTVDescSize = impl_->device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    impl_->swapChainBuffers.resize(desc.imageCount);

    CD3DX12_CPU_DESCRIPTOR_HANDLE descHandle(
        impl_->RTVDescHeap->GetCPUDescriptorHandleForHeapStart());
    for(int i = 0; i < desc.imageCount; ++i)
    {
        AGZ_D3D12_CHECK_HR_MSG(
            "failed to get swap chain buffer",
            impl_->swapChain->GetBuffer(
                i, IID_PPV_ARGS(impl_->swapChainBuffers[i].GetAddressOf())));

        impl_->device->CreateRenderTargetView(
            impl_->swapChainBuffers[i].Get(), nullptr, descHandle);

        descHandle.Offset(1, impl_->RTVDescSize);
    }

    // command queue fence

    AGZ_D3D12_CHECK_HR_MSG(
        "failed to create command queue fence",
        impl_->device->CreateFence(
            impl_->queueFenceValue, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(impl_->queueFence.GetAddressOf())));
}

void Window::initKeyboardAndMouse()
{
    impl_->keyboard = std::make_unique<Keyboard>();
    impl_->mouse = std::make_unique<Mouse>(impl_->hWindow);
}

Window::Window(const WindowDesc &desc)
{
    initWin32Window(desc);
    initD3D12(desc);
    initKeyboardAndMouse();
}

Window::~Window()
{
    if(!impl_)
        return;

    // IMPROVE
    try { waitCommandQueueIdle(); } catch(...) { }

    impl_->queueFence.Reset();

    impl_->swapChainBuffers.clear();
    impl_->RTVDescHeap.Reset();

    if(impl_->swapChain)
        impl_->swapChain->SetFullscreenState(FALSE, nullptr);

    impl_->swapChain.Reset();
    impl_->cmdQueue.Reset();
    impl_->device.Reset();

#ifdef AGZ_DEBUG
    {
        ComPtr<IDXGIDebug> dxgiDebug;
        if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
        {
            dxgiDebug->ReportLiveObjects(
                DXGI_DEBUG_ALL,
                DXGI_DEBUG_RLO_FLAGS(
                    DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
        }
    }
#endif

    if(impl_->hWindow)
    {
        impl::handleToWindow().erase(impl_->hWindow);
        DestroyWindow(impl_->hWindow);
    }

    UnregisterClassW(impl_->className.c_str(), impl_->hInstance);
}

void Window::doEvents()
{
    impl_->keyboard->_startUpdating();

    MSG msg;
    while(PeekMessage(&msg, impl_->hWindow, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    impl_->mouse->_update();

    impl_->keyboard->_endUpdating();
}

bool Window::isInFocus() const noexcept
{
    return impl_->inFocus;
}

void Window::waitForFocus()
{
    if(isInFocus())
        return;

    auto mouse = getMouse();

    const bool showCursor = mouse->isVisible();
    const bool lockCursor = mouse->isLocked();
    const int  lockX      = mouse->getLockX();
    const int  lockY      = mouse->getLockY();

    mouse->showCursor(true);
    mouse->setCursorLock(false, lockX, lockY);

    do
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        doEvents();

    } while(!isInFocus());

    mouse->showCursor(showCursor);
    mouse->setCursorLock(lockCursor, lockX, lockY);
    mouse->_update();
}

bool Window::getCloseFlag() const noexcept
{
    return impl_->shouldClose;
}

void Window::setCloseFlag(bool closeFlag) noexcept
{
    impl_->shouldClose = closeFlag;
}

Keyboard *Window::getKeyboard() const noexcept
{
    return impl_->keyboard.get();
}

Mouse *Window::getMouse() const noexcept
{
    return impl_->mouse.get();
}

int Window::getImageCount() const noexcept
{
    return impl_->swapChainImageCount;
}

int Window::getCurrentImageIndex() const
{
    return static_cast<int>(impl_->swapChain->GetCurrentBackBufferIndex());
}

ComPtr<ID3D12Resource> Window::getImage(int index) const noexcept
{
    return impl_->swapChainBuffers[index];
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Window::getImageDescHandle(int index) const noexcept
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        impl_->RTVDescHeap->GetCPUDescriptorHandleForHeapStart(),
        index, impl_->RTVDescSize);
}

UINT Window::getImageDescSize() const noexcept
{
    return impl_->RTVDescSize;
}

void Window::present() const noexcept
{
    impl_->swapChain->Present(0, 0);
}

ID3D12Device *Window::getDevice()
{
    return impl_->device.Get();
}

ID3D12CommandQueue *Window::getCommandQueue()
{
    return impl_->cmdQueue.Get();
}

void Window::waitCommandQueueIdle()
{
    ++impl_->queueFenceValue;

    impl_->cmdQueue->Signal(impl_->queueFence.Get(), impl_->queueFenceValue);

    if(impl_->queueFence->GetCompletedValue() < impl_->queueFenceValue)
    {
        HANDLE eventHandle = CreateEventEx(
            nullptr, false, false, EVENT_ALL_ACCESS);

        AGZ_D3D12_CHECK_HR_MSG(
            "failed to set command queue fence event",
            impl_->queueFence->SetEventOnCompletion(
                impl_->queueFenceValue, eventHandle));

        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void Window::_msgClose()
{
    impl_->shouldClose = true;
    eventMgr_.send(WindowCloseEvent{});
}

void Window::_msgKeyDown(KeyCode keycode)
{
    if(keycode != KEY_UNKNOWN)
        impl_->keyboard->_msgDown(keycode);
}

void Window::_msgKeyUp(KeyCode keycode)
{
    if(keycode != KEY_UNKNOWN)
        impl_->keyboard->_msgUp(keycode);
}

void Window::_msgCharInput(uint32_t ch)
{
    impl_->keyboard->_msgChar(ch);
}

void Window::_msgRawKeyDown(uint32_t vk)
{
    impl_->keyboard->_msgRawDown(vk);
}

void Window::_msgRawKeyUp(uint32_t vk)
{
    impl_->keyboard->_msgRawUp(vk);
}

void Window::_msgGetFocus()
{
    impl_->inFocus = true;
    eventMgr_.send(WindowGetFocusEvent{});
}

void Window::_msgLostFocus()
{
    impl_->inFocus = false;
    eventMgr_.send(WindowLostFocusEvent{});
}

void Window::_msgResize()
{
    waitCommandQueueIdle();

    eventMgr_.send(WindowPreResizeEvent{});

    // new client size

    RECT clientRect;
    GetClientRect(impl_->hWindow, &clientRect);
    impl_->clientWidth  = clientRect.right - clientRect.left;
    impl_->clientHeight = clientRect.bottom - clientRect.top;

    // resize swap chain buffers

    for(auto &b : impl_->swapChainBuffers)
        b.Reset();

    AGZ_D3D12_CHECK_HR_MSG(
        "failed to resize swap chain buffers",
        impl_->swapChain->ResizeBuffers(
            impl_->swapChainImageCount,
            impl_->clientWidth, impl_->clientHeight,
            impl::SWAP_CHAIN_BUFFER_FORMAT,
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    // rtv desc heap

    CD3DX12_CPU_DESCRIPTOR_HANDLE descHandle(
        impl_->RTVDescHeap->GetCPUDescriptorHandleForHeapStart());
    for(int i = 0; i < impl_->swapChainImageCount; ++i)
    {
        AGZ_D3D12_CHECK_HR_MSG(
            "failed to get swap chain buffer",
            impl_->swapChain->GetBuffer(
                i, IID_PPV_ARGS(impl_->swapChainBuffers[i].GetAddressOf())));

        impl_->device->CreateRenderTargetView(
            impl_->swapChainBuffers[i].Get(), nullptr, descHandle);

        descHandle.Offset(1, impl_->RTVDescSize);
    }

    eventMgr_.send(WindowPostResizeEvent{});
}

LRESULT impl::windowMessageProc(
    HWND hWindow, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const auto winIt = handleToWindow().find(hWindow);
    if(winIt == handleToWindow().end())
        return DefWindowProcW(hWindow, msg, wParam, lParam);
    const auto win = winIt->second;

    win->getMouse()->_msg(msg, wParam);

    switch(msg)
    {
    case WM_CLOSE:
        win->_msgClose();
        return 0;
    case WM_KEYDOWN:
        win->_msgKeyDown(
            event::keycode::win_vk_to_keycode(static_cast<int>(wParam)));
        [[fallthrough]];
    case WM_SYSKEYDOWN:
        win->_msgRawKeyDown(static_cast<uint32_t>(wParam));
        return 0;
    case WM_KEYUP:
        win->_msgKeyUp(
            event::keycode::win_vk_to_keycode(static_cast<int>(wParam)));
        [[fallthrough]];
    case WM_SYSKEYUP:
        win->_msgRawKeyUp(static_cast<uint32_t>(wParam));
        return 0;
    case WM_CHAR:
        if(wParam > 0 && wParam < 0x10000)
            win->_msgCharInput(static_cast<uint32_t>(wParam));
        break;
    case WM_SETFOCUS:
        win->_msgGetFocus();
        break;
    case WM_KILLFOCUS:
        win->_msgLostFocus();
        break;
    case WM_SIZE:
        if(wParam != SIZE_MINIMIZED)
            win->_msgResize();
        break;
    default:
        return DefWindowProcW(hWindow, msg, wParam, lParam);
    }

    return DefWindowProcW(hWindow, msg, wParam, lParam);
}

AGZ_D3D12_LAB_END
