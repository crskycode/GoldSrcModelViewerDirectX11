#include <Windows.h>
#include <wrl/client.h>
#include <atlbase.h>
#include <wincodec.h> // For WIC
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>


using Microsoft::WRL::ComPtr;

#include "StudioModelRenderer.hpp"


#ifdef _MSC_VER
#pragma comment ( lib, "d3d11.lib" )
#pragma comment ( lib, "Dxgi.lib" )
#pragma comment ( lib, "dxguid.lib" )
#endif

//#define RENDER_TO_BITMAP


// ------------------------------------------------------------
//                        Window Variables
// ------------------------------------------------------------

static constexpr uint32_t SCREEN_WIDTH = 800;
static constexpr uint32_t SCREEN_HEIGHT = 600;

constexpr TCHAR g_szClassName[] = TEXT("D3D11WindowClass");

HWND g_hwnd = nullptr;


// ------------------------------------------------------------
//                        DirectX Variables
// ------------------------------------------------------------

D3D_DRIVER_TYPE g_driverType = D3D_DRIVER_TYPE_NULL;
D3D_FEATURE_LEVEL g_featureLevel = D3D_FEATURE_LEVEL_11_0;

ComPtr<ID3D11Device> g_D3DDevice;
ComPtr<ID3D11Device1> g_D3DDevice1;
ComPtr<ID3D11DeviceContext> g_D3DDeviceContext;
ComPtr<ID3D11DeviceContext1> g_D3DDeviceContext1;
ComPtr<IDXGISwapChain> g_SwapChain;
ComPtr<IDXGISwapChain1> g_SwapChain1;
ComPtr<ID3D11RenderTargetView> g_RenderTargetView;
ComPtr<ID3D11Texture2D> g_DepthStencilTexture;
ComPtr<ID3D11DepthStencilView> g_DepthStencilView;
ComPtr<ID3D11RasterizerState> g_RasterizerState;
ComPtr<ID3D11Debug> g_D3DDebug;

#ifdef RENDER_TO_BITMAP
ComPtr<ID3D11Texture2D> g_RenderTargetTexture;
ComPtr<ID3D11Texture2D> g_BufferTexture;
#endif


// ------------------------------------------------------------
//                      Function identifiers
// ------------------------------------------------------------

HRESULT InitD3D(HWND hWnd);
void CleanD3D();
void RenderFrame();
void LoadModel();

#ifdef RENDER_TO_BITMAP
void SaveImage();
#endif


// ------------------------------------------------------------
//                        Our Model
// ------------------------------------------------------------

static std::unique_ptr<D3DStudioModel> g_d3dStudioModel;
static std::unique_ptr<D3DStudioModelRenderer> g_d3dStudioModelRenderer;


template<UINT TNameLength>
inline void SetDebugObjectName(_In_ ID3D11DeviceChild* resource, _In_z_ const char(&name)[TNameLength])
{
#if defined(_DEBUG) || defined(PROFILE)
	resource->SetPrivateData(WKPDID_D3DDebugObjectName, TNameLength - 1, name);
#endif
}


static std::vector<std::wstring> ParseCommandLine()
{
	auto line = GetCommandLineW();

	auto argc = 0;
	auto argv = CommandLineToArgvW(line, &argc);

	if (!argv)
		return {};

	std::vector<std::wstring> args;

	for (int i = 0; i < argc; ++i)
	{
		std::wstring wstrArg = argv[i];
		args.push_back(wstrArg);
	}

	LocalFree(argv);

	return args;
}


static std::string UnicodeToAnsi(const std::wstring& source)
{
	if (source.length() == 0)
		return std::string();

	int length = WideCharToMultiByte(CP_ACP, 0, source.c_str(), (int)source.length(), NULL, 0, NULL, NULL);

	if (length <= 0)
		return std::string();

	std::string output(length, '\0');

	if (WideCharToMultiByte(CP_ACP, 0, source.c_str(), (int)source.length(), (LPSTR)output.data(), (int)output.length() + 1, NULL, NULL) == 0)
		return std::string();

	return output;
}


#ifndef RENDER_TO_BITMAP

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_CLOSE:
			DestroyWindow(hwnd);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}


static int CreateRendererWindow(HINSTANCE hInstance, int nShowCmd)
{
	WNDCLASSEX wc{};
	MSG msg;

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = g_szClassName;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error!"), MB_ICONEXCLAMATION | MB_OK);
		return EXIT_FAILURE;
	}

	RECT wr = { 0,0, SCREEN_WIDTH, SCREEN_HEIGHT };
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

	g_hwnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		g_szClassName,
		TEXT("GoldSrc Model Viewer on DirectX 11"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wr.right - wr.left,
		wr.bottom - wr.top,
		NULL,
		NULL,
		hInstance,
		NULL
	);

	if (!g_hwnd)
	{
		MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error!"), MB_ICONEXCLAMATION | MB_OK);
		return EXIT_FAILURE;
	}

	ShowWindow(g_hwnd, nShowCmd);
	UpdateWindow(g_hwnd);

	try
	{
		HRESULT hr = InitD3D(g_hwnd);

		if (FAILED(hr))
		{
			CleanD3D();
			return EXIT_FAILURE;
		}

		while (true)
		{

			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				if (msg.message == WM_QUIT)
					break;
			}

			RenderFrame();
			Sleep(20);
		}

		CleanD3D();
		return static_cast<int>(msg.wParam);
	}
	catch (const std::exception& e)
	{
		MessageBoxA(g_hwnd, e.what(), "Error!", MB_ICONERROR | MB_OK);
		CleanD3D();
		return EXIT_FAILURE;
	}
	catch (...)
	{
		MessageBox(g_hwnd, TEXT("Caught an unknown exception."), TEXT("Error!"), MB_ICONERROR | MB_OK);
		CleanD3D();
		return EXIT_FAILURE;
	}
}

#else

static int RenderToBitmap()
{
	try
	{
		HRESULT hr = InitD3D(NULL);

		if (FAILED(hr))
		{
			CleanD3D();
			return EXIT_FAILURE;
		}

		RenderFrame();
		SaveImage();
		CleanD3D();
	}
	catch (const std::exception& e)
	{
		MessageBoxA(g_hwnd, e.what(), "Error!", MB_ICONERROR | MB_OK);
		CleanD3D();
		return EXIT_FAILURE;
	}
	catch (...)
	{
		MessageBox(g_hwnd, TEXT("Caught an unknown exception."), TEXT("Error!"), MB_ICONERROR | MB_OK);
		CleanD3D();
		return EXIT_FAILURE;
	}

	return 0;
}

#endif


int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd)
{
#ifndef RENDER_TO_BITMAP
	return CreateRendererWindow(hInstance, nShowCmd);
#else
	return RenderToBitmap();
#endif
}


HRESULT InitD3D(HWND hWnd)
{
	HRESULT hr;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
	{
		g_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDevice(nullptr, g_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, g_D3DDevice.ReleaseAndGetAddressOf(), &g_featureLevel, g_D3DDeviceContext.ReleaseAndGetAddressOf());

		if (hr == E_INVALIDARG)
		{
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
			hr = D3D11CreateDevice(nullptr, g_driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
				D3D11_SDK_VERSION, g_D3DDevice.ReleaseAndGetAddressOf(), &g_featureLevel, g_D3DDeviceContext.ReleaseAndGetAddressOf());
		}

		if (SUCCEEDED(hr))
			break;
	}

	if (FAILED(hr))
		return hr;

#if _DEBUG
	hr = g_D3DDevice->QueryInterface(IID_PPV_ARGS(g_D3DDebug.ReleaseAndGetAddressOf()));

	if (FAILED(hr))
		return hr;
#endif

	UINT m4xMsaaQuality;
	hr = g_D3DDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &m4xMsaaQuality);

	if (FAILED(hr))
		return hr;

#ifndef RENDER_TO_BITMAP

	//
	// We need a swap chain to present the rendered content to the window
	//

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	ComPtr<IDXGIFactory1> dxgiFactory1;

	{
		ComPtr<IDXGIDevice> dxgiDevice;
		hr = g_D3DDevice->QueryInterface(IID_PPV_ARGS(dxgiDevice.ReleaseAndGetAddressOf()));

		if (FAILED(hr))
			return hr;

		ComPtr<IDXGIAdapter> dxgiAdapter;
		hr = dxgiDevice->GetAdapter(dxgiAdapter.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		hr = dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory1.ReleaseAndGetAddressOf()));

		if (FAILED(hr))
			return hr;
	}

	// Try to get IDXGIFactory2 if it's available
	ComPtr<IDXGIFactory2> dxgiFactory2;
	hr = dxgiFactory1->QueryInterface(IID_PPV_ARGS(dxgiFactory2.ReleaseAndGetAddressOf()));

	if (SUCCEEDED(hr))
	{
		// DirectX 11.1 or later
		hr = g_D3DDevice->QueryInterface(IID_PPV_ARGS(g_D3DDevice1.ReleaseAndGetAddressOf()));

		if (SUCCEEDED(hr))
		{
			hr = g_D3DDeviceContext->QueryInterface(IID_PPV_ARGS(g_D3DDeviceContext1.ReleaseAndGetAddressOf()));

			if (FAILED(hr))
				return hr;
		}

		DXGI_SWAP_CHAIN_DESC1 sd{};
		sd.Width = SCREEN_WIDTH;
		sd.Height = SCREEN_HEIGHT;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 1;

		hr = dxgiFactory2->CreateSwapChainForHwnd(g_D3DDevice.Get(), hWnd, &sd, nullptr, nullptr, g_SwapChain1.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;

		hr = g_SwapChain1->QueryInterface(IID_PPV_ARGS(g_SwapChain.ReleaseAndGetAddressOf()));

		if (FAILED(hr))
			return hr;

		dxgiFactory2.Reset();
	}
	else
	{
		// DirectX 11.0 systems
		DXGI_SWAP_CHAIN_DESC sd{};
		sd.BufferCount = 1;
		sd.BufferDesc.Width = SCREEN_WIDTH;
		sd.BufferDesc.Height = SCREEN_HEIGHT;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hWnd;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;

		hr = dxgiFactory1->CreateSwapChain(g_D3DDevice.Get(), &sd, g_SwapChain.ReleaseAndGetAddressOf());

		if (FAILED(hr))
			return hr;
	}

	// Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
	hr = dxgiFactory1->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER);

	if (FAILED(hr))
		return hr;

	dxgiFactory1.Reset();

	//
	// Get the render buffer from the swap chain and create a RenderTargetView
	//

	{
		ComPtr<ID3D11Texture2D> backBufferTexture;

		hr = g_SwapChain->GetBuffer(0, IID_PPV_ARGS(backBufferTexture.ReleaseAndGetAddressOf()));
		if (FAILED(hr))
			return hr;

		hr = g_D3DDevice->CreateRenderTargetView(backBufferTexture.Get(), nullptr, g_RenderTargetView.ReleaseAndGetAddressOf());
		if (FAILED(hr))
			return hr;
	}

#else

	//
	// Create a texture as render target
	//

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = SCREEN_WIDTH;
	desc.Height = SCREEN_HEIGHT;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	hr = g_D3DDevice->CreateTexture2D(&desc, NULL, g_RenderTargetTexture.ReleaseAndGetAddressOf());

	if (FAILED(hr))
		return hr;

	//
	// Create a buffer
	//

	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	hr = g_D3DDevice->CreateTexture2D(&desc, NULL, g_BufferTexture.ReleaseAndGetAddressOf());

	if (FAILED(hr))
		return hr;

	//
	// Create view of the render target
	//

	hr = g_D3DDevice->CreateRenderTargetView(g_RenderTargetTexture.Get(), NULL, g_RenderTargetView.ReleaseAndGetAddressOf());

	if (FAILED(hr))
		return hr;

#endif

	// 
	// Create texture for depth stencil
	//

	D3D11_TEXTURE2D_DESC dstd{};
	dstd.Width = SCREEN_WIDTH;
	dstd.Height = SCREEN_HEIGHT;
	dstd.MipLevels = 1;
	dstd.ArraySize = 1;
	dstd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dstd.SampleDesc.Count = 1;
	dstd.SampleDesc.Quality = 0;
	dstd.Usage = D3D11_USAGE_DEFAULT;
	dstd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	dstd.CPUAccessFlags = 0;
	dstd.MiscFlags = 0;

	hr = g_D3DDevice->CreateTexture2D(&dstd, nullptr, g_DepthStencilTexture.ReleaseAndGetAddressOf());

	if (FAILED(hr))
		return hr;

	//
	// Create view of depth stencil texture
	//

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
	dsvd.Format = dstd.Format;
	dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvd.Texture2D.MipSlice = 0;

	hr = g_D3DDevice->CreateDepthStencilView(g_DepthStencilTexture.Get(), 0, g_DepthStencilView.ReleaseAndGetAddressOf());

	if (FAILED(hr))
		return hr;

	//
	// Our render target view and depth stencil view are ready
	//

	ID3D11RenderTargetView* renderTargetViews[] =
	{
		g_RenderTargetView.Get()
	};

	g_D3DDeviceContext->OMSetRenderTargets(ARRAYSIZE(renderTargetViews), renderTargetViews, g_DepthStencilView.Get());

	//
	// Create rasterizer state
	//

	D3D11_RASTERIZER_DESC rasterDesc{};
	rasterDesc.AntialiasedLineEnable = false;
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = 0;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = true;
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.FrontCounterClockwise = false;
	rasterDesc.MultisampleEnable = false;
	rasterDesc.ScissorEnable = false;
	rasterDesc.SlopeScaledDepthBias = 0.0f;

	hr = g_D3DDevice->CreateRasterizerState(&rasterDesc, g_RasterizerState.ReleaseAndGetAddressOf());

	if (FAILED(hr))
		return hr;

	//
	// Use rasterizer state
	//

	g_D3DDeviceContext->RSSetState(g_RasterizerState.Get());

	//
	// Finally, we setup the viewport
	//

	D3D11_VIEWPORT viewPort{};
	viewPort.TopLeftX = 0;
	viewPort.TopLeftY = 0;
	viewPort.MinDepth = 0.0f;
	viewPort.MaxDepth = 1.0f;
	viewPort.Width = SCREEN_WIDTH;
	viewPort.Height = SCREEN_HEIGHT;

	g_D3DDeviceContext->RSSetViewports(1, &viewPort);

	//
	// Now the window is ready
	//

	LoadModel();

	return hr;
}


void CleanD3D()
{
	if (g_SwapChain)
		g_SwapChain->SetFullscreenState(FALSE, nullptr);

	g_d3dStudioModelRenderer.reset();
	g_d3dStudioModel.reset();

	g_RasterizerState.Reset();
	g_DepthStencilView.Reset();
	g_DepthStencilTexture.Reset();
	g_RenderTargetView.Reset();
	g_SwapChain.Reset();
	g_SwapChain1.Reset();
	g_D3DDeviceContext1.Reset();
	g_D3DDevice1.Reset();
	g_D3DDeviceContext.Reset();

#ifdef RENDER_TO_BITMAP
	g_RenderTargetTexture.Reset();
	g_BufferTexture.Reset();
#endif

#if _DEBUG
	if (g_D3DDebug)
	{
		OutputDebugString(TEXT("Dumping DirectX 11 live objects.\n"));
		g_D3DDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
		g_D3DDebug.Reset();
	}
#endif

	g_D3DDevice.Reset();
}


void RenderFrame()
{
	float clearColor[4] = { 0.2f, 0.5f, 0.698f, 1.0f };
	g_D3DDeviceContext->ClearRenderTargetView(g_RenderTargetView.Get(), clearColor);
	g_D3DDeviceContext->ClearDepthStencilView(g_DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	if (g_d3dStudioModel && g_d3dStudioModelRenderer)
	{
#ifndef RENDER_TO_BITMAP
		RECT rc{};
		GetClientRect(g_hwnd, &rc);

		int w = rc.right - rc.left;
		int h = rc.bottom - rc.top;
#else
		int w = SCREEN_WIDTH;
		int h = SCREEN_HEIGHT;
#endif

		g_d3dStudioModelRenderer->SetViewport(w, h);
		g_d3dStudioModelRenderer->SetModel(g_d3dStudioModel.get());
		g_d3dStudioModelRenderer->Draw();
	}

#ifndef RENDER_TO_BITMAP
	g_SwapChain->Present(0, 0);
#else
	g_D3DDeviceContext->Flush();
#endif
}


#ifdef RENDER_TO_BITMAP

void SaveImage()
{
	HRESULT hr;

	// Copy from render target to buffer
	g_D3DDeviceContext->CopyResource(g_BufferTexture.Get(), g_RenderTargetTexture.Get());

	D3D11_MAPPED_SUBRESOURCE mappedResource{};
	static const UINT resourceId = D3D11CalcSubresource(0, 0, 0);

	hr = g_D3DDeviceContext->Map(g_BufferTexture.Get(), resourceId, D3D11_MAP_READ, 0, &mappedResource);
	if (FAILED(hr))
		return;

	hr = CoInitialize(NULL);
	if (FAILED(hr))
		return;

	CComPtr<IWICImagingFactory> factory;
	hr = factory.CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER);
	if (FAILED(hr))
		return;

	CComPtr<IWICBitmap> wicBitmap;
	hr = factory->CreateBitmapFromMemory(
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
		GUID_WICPixelFormat32bppRGBA,
		mappedResource.RowPitch,
		mappedResource.DepthPitch,
		reinterpret_cast<BYTE*>(mappedResource.pData),
		&wicBitmap);

	if (FAILED(hr))
		return;

	g_D3DDeviceContext->Unmap(g_BufferTexture.Get(), resourceId);

	CComPtr<IWICStream> wicStream;
	CComPtr<IWICBitmapEncoder> wicBitmapEncoder;
	CComPtr<IWICBitmapFrameEncode> wicBitmapFrameEncode;

	hr = factory->CreateStream(&wicStream);
	if (FAILED(hr))
		return;

	hr = wicStream->InitializeFromFilename(L"output.png", GENERIC_WRITE);
	if (FAILED(hr))
		return;

	hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &wicBitmapEncoder);
	if (FAILED(hr))
		return;

	hr = wicBitmapEncoder->Initialize(wicStream, WICBitmapEncoderNoCache);
	if (FAILED(hr))
		return;

	hr = wicBitmapEncoder->CreateNewFrame(&wicBitmapFrameEncode, NULL);
	if (FAILED(hr))
		return;

	hr = wicBitmapFrameEncode->Initialize(NULL);
	if (FAILED(hr))
		return;

	hr = wicBitmapFrameEncode->WriteSource(wicBitmap, NULL);
	if (FAILED(hr))
		return;

	hr = wicBitmapFrameEncode->Commit();
	if (FAILED(hr))
		return;

	hr = wicBitmapEncoder->Commit();
	if (FAILED(hr))
		return;
}

#endif


void LoadModel()
{
	g_d3dStudioModelRenderer = std::make_unique<D3DStudioModelRenderer>();
	g_d3dStudioModelRenderer->Init(g_D3DDevice.Get(), g_D3DDeviceContext.Get());

	g_d3dStudioModel = std::make_unique<D3DStudioModel>();
	g_d3dStudioModel->Load(g_D3DDevice.Get(), "vip.mdl");
}
