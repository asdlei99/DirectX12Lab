#pragma once

#include <chrono>
#include <string>

#include <d3d12.h>

#include <agz/d3d12/descriptor/rawDescriptorHeap.h>
#include <agz/d3d12/pipeline/pipelineState.h>
#include <agz/d3d12/sync/cmdQueueWaiter.h>
#include <agz/d3d12/window/keyboard.h>
#include <agz/d3d12/window/mouse.h>
#include <agz/d3d12/window/windowEvent.h>

AGZ_D3D12_BEGIN

struct WindowDesc
{
    // window

    int clientWidth  = 640;
    int clientHeight = 480;

    std::wstring title = L"D3D12 Lab";

    bool resizable  = true;
    bool fullscreen = false;

    // d3d12

    bool vsync = true;

    DXGI_FORMAT backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    int imageCount = 2;

    DWORD getStyle() const noexcept;
};

struct WindowImplData;

class Window
{
public:

    explicit Window(const WindowDesc &desc);

    ~Window();

    // win32 events

    void doEvents();

    bool isInFocus() const noexcept;

    void waitForFocus();

    // close flag

    bool getCloseFlag() const noexcept;

    void setCloseFlag(bool closeFlag) noexcept;

    // keyboard/mouse

    Keyboard *getKeyboard() const noexcept;

    Mouse *getMouse() const noexcept;

    // swap chain image

    int getImageCount() const noexcept;

    int getCurrentImageIndex() const;

    ID3D12Resource *getCurrentImage() const noexcept;

    D3D12_CPU_DESCRIPTOR_HANDLE getCurrentImageDescHandle() const noexcept;

    UINT getImageDescSize() const noexcept;

    void present() const;

    int getImageWidth() const noexcept;

    int getImageHeight() const noexcept;

    float getImageWOverH() const noexcept;

    DXGI_FORMAT getImageFormat() const noexcept;

    const D3D12_VIEWPORT &getDefaultViewport() const noexcept;

    const D3D12_RECT &getDefaultScissorRect() const noexcept;

    // d3d12 device/queue

    ID3D12Device *getDevice() const noexcept;

    ID3D12CommandQueue *getCommandQueue() const noexcept;

    void executeOneCmdList(ID3D12CommandList *cmdList) const noexcept;

    void waitCommandQueueIdle() const;

    // fast create

    ComPtr<ID3D12Fence> createFence(
        UINT64            initValue,
        D3D12_FENCE_FLAGS flags) const;

    ComPtr<ID3D12CommandAllocator> createCommandAllocator(
        D3D12_COMMAND_LIST_TYPE type) const;

    ComPtr<ID3D12GraphicsCommandList> createGraphicsCommandList(
        UINT                    nodeMask,
        D3D12_COMMAND_LIST_TYPE type,
        ID3D12CommandAllocator *cmdAlloc,
        ID3D12PipelineState    *initPipeline);

    CommandQueueWaiter createCmdQueueWaiter() const;

    GraphicsPipelineStateBuilder createPipelineBuilder() const;

    RawDescriptorHeap createDescriptorHeap(
        int size, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible) const;

    // event handler manager

    AGZ_D3D12_DECL_EVENT_MGR_HANDLER(eventMgr_, WindowCloseEvent)
    AGZ_D3D12_DECL_EVENT_MGR_HANDLER(eventMgr_, WindowGetFocusEvent)
    AGZ_D3D12_DECL_EVENT_MGR_HANDLER(eventMgr_, WindowLostFocusEvent)
    AGZ_D3D12_DECL_EVENT_MGR_HANDLER(eventMgr_, WindowPreResizeEvent)
    AGZ_D3D12_DECL_EVENT_MGR_HANDLER(eventMgr_, WindowPostResizeEvent)

    // for internal use

    void _msgClose();

    void _msgKeyDown(KeyCode keycode);

    void _msgKeyUp(KeyCode keycode);

    void _msgCharInput(uint32_t ch);

    void _msgRawKeyDown(uint32_t vk);

    void _msgRawKeyUp(uint32_t vk);

    void _msgGetFocus();

    void _msgLostFocus();

    void _msgResize();

private:

    void initWin32Window(const WindowDesc &desc);

    void initD3D12(const WindowDesc &desc);

    void initKeyboardAndMouse();

    std::unique_ptr<WindowImplData> impl_;

    WindowEventManager eventMgr_;
};

AGZ_D3D12_END
