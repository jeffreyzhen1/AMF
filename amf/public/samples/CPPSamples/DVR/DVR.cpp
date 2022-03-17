// 
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
// 
// MIT license 
// 
//
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// DVR.cpp : Defines the entry point for the application.
//

#include "targetver.h"

//#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <commctrl.h>

// C RunTime Header Files
#include <atlbase.h>
#include <d3d11.h>
#include <stdlib.h>
#include <tchar.h>
#include <objbase.h>

#include "DVR.h"

#include "public/common/AMFFactory.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/core/Debug.h"

#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/DisplayDvrPipeline.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"



#pragma comment(linker, \
  "\"/manifestdependency:type='Win32' "\
  "name='Microsoft.Windows.Common-Controls' "\
  "version='6.0.0.0' "\
  "processorArchitecture='*' "\
  "publicKeyToken='6595b64144ccf1df' "\
  "language='*'\"")

#pragma comment(lib, "ComCtl32.lib")




// Global Variables:
HINSTANCE hInst  = NULL;                                // current instance
HWND      g_hDlg = NULL;

// Resolution of the desktops that could be captured
struct CaptureSource 
{
	int			ResolutionX;
	int			ResolutionY;
	unsigned	deviceIdx;
	unsigned	adapterIdx;
};

static std::vector<std::wstring>  s_vAdapters;
static std::vector<CaptureSource> s_vDisplays;

const unsigned __int64 ID_STATUS_BAR    = 100;
const unsigned         TIMER_ID         = 10000;

static bool     s_bOCLConverter         = false;
static UINT_PTR s_uiFpsTimerId          = 0;
static unsigned kDefaultGPUIdx          = 0;

static DisplayDvrPipeline* s_pPipeline = NULL;



// Forward declarations of functions included in this code module:
void                CreateStatusBar(HWND hwndParent, HINSTANCE hinst);
INT_PTR CALLBACK    DialogProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Dialog(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    AboutDialog(HWND, UINT, WPARAM, LPARAM);

void                UpdateMenuItems();
void                PopulateMenus(HMENU hMenu);
void                PopulateAdaptersMenu(HMENU hMenu);
void                PopulateDisplaysMenu(HMENU hMenu);
void                PopulateComponentsMenu(HMENU hMenu);

void                ChangeFileLocation(HWND hWnd);
void                GetDirectoryFileLocation(TCHAR* szDirectory, TCHAR* szFile, bool generic = false);
void                GetDefaultFileLocation(TCHAR* szFile, bool generic = false);
void                GetCurrentFileLocation(TCHAR* szFile);
void                GetRecordFileLocation(TCHAR* szFile);

void                StartRecording(HWND hWnd);
void                StopRecording(HWND hWnd);
void                FailedRecording(HWND hWnd, bool init);

void                UpdateButtons(HWND hWnd, bool isRecording);
void                UpdateFps(HWND hWnd);
void                UpdateMessage(const wchar_t *msg);



//-------------------------------------------------------------------------------------------------
// define some wrapper classes to guarantee 
// initialization and proper cleanup
class CComInit
{
public:
    CComInit()  {  CoInitializeEx(NULL, COINIT_MULTITHREADED);  };
    ~CComInit() {  CoUninitialize();  };
};
//-------------------------------------------------------------------------------------------------
class CAmfInit
{
public:
    CAmfInit()  {};
    ~CAmfInit() {  g_AMFFactory.Terminate();  };

    AMF_RESULT Init()
    {
        AMF_RESULT res = g_AMFFactory.Init();
        if (res != AMF_OK)
            return res;

#ifdef _DEBUG
        g_AMFFactory.GetDebug()->AssertsEnable(true);
#else
        g_AMFFactory.GetDebug()->AssertsEnable(false);
//		g_AMFFactory.GetTrace()->SetGlobalLevel(AMF_TRACE_WARNING);
#endif

        g_AMFFactory.GetTrace()->SetGlobalLevel(AMF_TRACE_INFO);
        g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_INFO);
        g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_CONSOLE, AMF_TRACE_INFO);

        return AMF_OK;
    }
};
//-------------------------------------------------------------------------------------------------




//-------------------------------------------------------------------------------------------------
int APIENTRY _tWinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPTSTR    lpCmdLine,
	int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
    hInst = hInstance; // Store instance handle in our global variable


	s_bOCLConverter = false;
    //-------------------------------------------------------------------------------------------------
    // Main
    // initialize required objects
    CComInit  comInit;
    CAmfInit  amfInit;
    AMF_RESULT res = amfInit.Init();
    if (res != AMF_OK)
    {
        LOG_ERROR(L"AMF failed to initialize");
        return 1;
    }

	AMFCustomTraceWriter writer(AMF_TRACE_WARNING);
	amf_increase_timer_precision();


	//-------------------------------------------------------------------------------------------------
	// DisplayDvrPipeline Initialization
	DisplayDvrPipeline pipeline;
	s_pPipeline = &pipeline;

	std::wstring codec = L"AMFVideoEncoderVCE_AVC";
	s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_CODEC, AMFVideoEncoderVCE_AVC);
	RegisterEncoderParamsAVC(s_pPipeline);


    s_pPipeline->SetParam(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
    s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, kDefaultGPUIdx);
    s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_MONITORID, 0);

    wchar_t *component = L"AMD";
    s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_CAPTURE_COMPONENT, component);

#if defined(_WIN32)
    if (parseCmdLineParameters(s_pPipeline))
#else
    if (parseCmdLineParameters(s_pPipeline, argc, argv))
#endif        


    //-------------------------------------------------------------------------------------------------
    // initialize dialog UI
    InitCommonControls();

    g_hDlg = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_MAIN_DIALOG), 0, DialogProc, 0);
    ShowWindow(g_hDlg, nCmdShow);
    UpdateWindow(g_hDlg);
    CreateStatusBar(g_hDlg, hInstance);
    PopulateMenus(GetMenu(g_hDlg));
    UpdateButtons(g_hDlg, false);

		
    //-------------------------------------------------------------------------------------------------
	// Main message loop:
    MSG  msg = {};
    BOOL ret;
    while (ret = GetMessage(&msg, NULL, 0, 0))
	{
        if (ret == -1)
            return -1;

        if (!IsDialogMessage(g_hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
	}

	return (int)msg.wParam;
}

// Description: 
//   Creates a status bar and divides it into 2 parts.
// Parameters:
//   hwndParent - parent window for the status bar.
//   hinst      - handle to the application instance.
//
void CreateStatusBar(HWND hwndParent, HINSTANCE hinst)
{
    // Create the status bar.
    HWND hwndStatus = CreateWindowEx(
        0,                       // no extended styles
        STATUSCLASSNAME,         // name of status bar class
        (PCTSTR)NULL,            // no text when first created
        SBARS_SIZEGRIP |         // includes a sizing grip
        WS_CHILD | WS_VISIBLE,   // creates a visible child window
        0, 0, 0, 0,              // ignores size and position
        hwndParent,              // handle to parent window
        (HMENU)ID_STATUS_BAR,    // child window identifier
        hinst,                   // handle to application instance
        NULL);                   // no window creation data

    // Get the coordinates of the parent window's client area.
    RECT rcClient;
    ::GetClientRect(hwndParent, &rcClient);

    // Allocate an array for holding the right edge coordinates.
    HLOCAL hloc    = LocalAlloc(LHND, sizeof(int) * 2);
    PINT   paParts = (PINT) LocalLock(hloc);

    // Calculate the right edge coordinate for each part, and
    // copy the coordinates to the array.
    paParts[0] = rcClient.right * 75 / 100;
    paParts[1] = rcClient.right;

    // Tell the status bar to create the window parts.
    ::SendMessage(hwndStatus, SB_SETPARTS, 2, (LPARAM) paParts);

    // Free the array, and return.
    LocalUnlock(hloc);
    LocalFree(hloc);
}

//-------------------------------------------------------------------------------------------------
// Populate devices, displays and components menus
//-------------------------------------------------------------------------------------------------
void PopulateMenus(HMENU hMenu)
{
	ATL::CComPtr<IDXGIFactory> pFactory;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
	if (FAILED(hr))
	{
		LOG_ERROR(L"CreateDXGIFactory failed.");
		return;
	}


    // Enumerate display devices
	for (UINT device = 0; ; ++device)
	{
        // get device - if no devices found anymore, exit the loop
		ATL::CComPtr<IDXGIAdapter> pDevice;
		if (pFactory->EnumAdapters(device, &pDevice) == DXGI_ERROR_NOT_FOUND)
		{
			break;
		}

		DXGI_ADAPTER_DESC descAdapter;
		hr = pDevice->GetDesc(&descAdapter);
		if (SUCCEEDED(hr))
		{
#ifdef _DEBUG
            std::wstring adapterMsg;
            adapterMsg += L"Found adapter: ";
            adapterMsg += descAdapter.Description;
            adapterMsg += ((descAdapter.VendorId != 0x1002) ? L": Non AMD" : L": AMD");
            UpdateMessage(adapterMsg.c_str());
#endif
            // Ensure only AMD GPUs are listed
            if (descAdapter.VendorId != 0x1002) 
            {
                continue;
            }

			// Add to the Devices list
            s_vAdapters.push_back(descAdapter.Description);


			// Enumerate monitors for the current adapter
            UINT  adapterIdx = 0;
            for (UINT output = 0; ; ++output)
			{
				ATL::CComPtr<IDXGIOutput> pOutput;
				if (pDevice->EnumOutputs(output, &pOutput) == DXGI_ERROR_NOT_FOUND)
				{
					break;
				}
				
				DXGI_OUTPUT_DESC descOutput;
				hr = pOutput->GetDesc(&descOutput);
				if (SUCCEEDED(hr))
				{
					// Get this output's screen resolution
					MONITORINFOEX monitorInfo;
					monitorInfo.cbSize = sizeof(MONITORINFOEX);
					GetMonitorInfo(descOutput.Monitor, &monitorInfo);

					DEVMODE devMode;
					devMode.dmSize = sizeof(DEVMODE);
					devMode.dmDriverExtra = 0;
					EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode);

					// Add to the Capture Sources list
					CaptureSource cs;
					cs.ResolutionX = devMode.dmPelsWidth;
					cs.ResolutionY = devMode.dmPelsHeight;
					cs.deviceIdx = device;		// GPU
					cs.adapterIdx = adapterIdx;	// ADAPTER on GPU
					s_vDisplays.push_back(cs);

                    ++adapterIdx;
				}
			}
		}
	}

	//
	if (s_vAdapters.empty() || s_vDisplays.empty())
	{
		// No devices detected, close application
		PostMessage(GetActiveWindow(), WM_USER + 1000, 0, 0);
	}
	else
	{
        PopulateAdaptersMenu(hMenu);
        PopulateDisplaysMenu(hMenu);
        PopulateComponentsMenu(hMenu);

		// Remove placeholder separators (needed because GetSubMenu returns NULL if menu is initially empty)
        HMENU  hDevices           = GetSubMenu(hMenu, 1);
        HMENU  hCaptureSources    = GetSubMenu(hMenu, 2);
        HMENU  hCaptureComponents = GetSubMenu(hMenu, 3);
        RemoveMenu(hDevices, 0, MF_BYPOSITION);
        RemoveMenu(hCaptureSources, 0, MF_BYPOSITION);
        RemoveMenu(hCaptureComponents, 0, MF_BYPOSITION);

		// Place default checkmarks at first item in each menu
		UpdateMenuItems();
	}
}

//-------------------------------------------------------------------------------------------------
// Populate adapters menu
//-------------------------------------------------------------------------------------------------
void  PopulateAdaptersMenu(HMENU hMenu)
{
    HMENU  hDevices = GetSubMenu(hMenu, 1);

    // update adapters
    int deviceId = ID_DEVICE_START;
    for (std::vector<std::wstring>::const_iterator it = s_vAdapters.begin(); it != s_vAdapters.end(); ++it)
    {
        if (AppendMenu(hDevices, MF_BYPOSITION, deviceId++, it->c_str()) == false)
        {
            LOG_ERROR(L"Could not insert device menu item.");
            break;
        }
    }
}

//-------------------------------------------------------------------------------------------------
// Populate displays menu
//-------------------------------------------------------------------------------------------------
void  PopulateDisplaysMenu(HMENU hMenu)
{
    HMENU  hCaptureSources = GetSubMenu(hMenu, 2);

    // update capture sources
    int outputId = ID_CAPTURE_SOURCE_START;
    for (std::vector<CaptureSource>::const_iterator it = s_vDisplays.begin(); it != s_vDisplays.end(); ++it, outputId++)
    {
        // Add to the Capture Sources menu
        TCHAR outputName[128];
        swprintf_s(outputName, 128, L"Monitor %d (%d x %d)", outputId - ID_CAPTURE_SOURCE_START, it->ResolutionX, it->ResolutionY);

        if (AppendMenu(hCaptureSources, MF_BYPOSITION, outputId, outputName) == false)
        {
            LOG_ERROR(L"Could not insert capture source menu item.");
            break;
        }
        if (EnableMenuItem(hCaptureSources, outputId, (it->deviceIdx == kDefaultGPUIdx) ? MF_ENABLED : MF_DISABLED))
        {
            LOG_ERROR(L"Could not enable/disable capture source menu item.");
            break;
        }
    }
}

//-------------------------------------------------------------------------------------------------
// Populate components menu
//-------------------------------------------------------------------------------------------------
void  PopulateComponentsMenu(HMENU hMenu)
{
    HMENU  hCaptureComponents = GetSubMenu(hMenu, 3);

    if (AppendMenu(hCaptureComponents, MF_BYPOSITION, ID_CAPTURE_COMPONENT_START, L"AMD DirectCapture") == false)
    {
        LOG_ERROR(L"Could not insert AMD capture component menu item.");
        return;
    }
    if (AppendMenu(hCaptureComponents, MF_BYPOSITION, ID_CAPTURE_COMPONENT_START + 1, L"Desktop Duplication") == false)
    {
        LOG_ERROR(L"Could not insert DD capture component menu item.");
    }
}

//-------------------------------------------------------------------------------------------------
// Update the menu for adapters/displays/components entries
//-------------------------------------------------------------------------------------------------
void UpdateMenuItems()
{
    int iSelectedDevice = 0, iSelectedCapture = 0;
    std::wstring iSelectedComponent = L"";
    s_pPipeline->GetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, iSelectedDevice);
    s_pPipeline->GetParam(DisplayDvrPipeline::PARAM_NAME_MONITORID, iSelectedCapture);
    s_pPipeline->GetParamWString(DisplayDvrPipeline::PARAM_NAME_CAPTURE_COMPONENT, iSelectedComponent);

    HMENU  hMenu = GetMenu(g_hDlg);
    HMENU  hDevices = GetSubMenu(hMenu, 1);
    for (int i = 0; i < (int)s_vAdapters.size(); ++i)
    {
        CheckMenuItem(hDevices, ID_DEVICE_START + i, MF_BYCOMMAND | (i == iSelectedDevice ? MF_CHECKED : MF_UNCHECKED));
    }

    HMENU  hCaptureSources = GetSubMenu(hMenu, 2);
    for (int i = 0; i < (int)s_vDisplays.size(); ++i)
    {
        const bool  enableItem = s_vDisplays[i].deviceIdx == iSelectedDevice;
        const bool  checkItem = enableItem && (s_vDisplays[i].adapterIdx == iSelectedCapture);
        CheckMenuItem(hCaptureSources, ID_CAPTURE_SOURCE_START + i, MF_BYCOMMAND | (checkItem ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(hCaptureSources, ID_CAPTURE_SOURCE_START + i, enableItem ? MF_ENABLED : MF_DISABLED);
    }

    HMENU  hCaptureComponents = GetSubMenu(hMenu, 3);
    if (iSelectedComponent == L"AMD")
    {
        CheckMenuItem(hCaptureComponents, ID_CAPTURE_COMPONENT_START, MF_BYCOMMAND | MF_CHECKED);
        CheckMenuItem(hCaptureComponents, ID_CAPTURE_COMPONENT_START + 1, MF_BYCOMMAND | MF_UNCHECKED);
    }
    else
    {
        CheckMenuItem(hCaptureComponents, ID_CAPTURE_COMPONENT_START + 1, MF_BYCOMMAND | MF_CHECKED);
        CheckMenuItem(hCaptureComponents, ID_CAPTURE_COMPONENT_START, MF_BYCOMMAND | MF_UNCHECKED);
    }

    s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OPENCL_CONVERTER, s_bOCLConverter);
}


//-------------------------------------------------------------------------------------------------
//
//  FUNCTION: DialogProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main dialog.
//
//  WM_COMMAND    - process the application menu
//  WM_DESTROY    - post a quit message and return
//
//
INT_PTR CALLBACK DialogProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;

	switch (message)
	{
	case WM_USER + 1000:
		// If no AMD devices are recognized the application closes
		if (DialogBox(hInst, MAKEINTRESOURCE(IDD_NODEVICEBOX), hWnd, Dialog) != IDOK)
		{
			PostQuitMessage(0);
		}
		break;
	case WM_COMMAND:
		wmId = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		if (wmId == IDM_ABOUT)
		{
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutDialog);
		}
        else if ((wmId == IDCANCEL) || (wmId == IDM_EXIT))
		{
			DestroyWindow(hWnd);
        }
		else if (wmId == IDB_RECORD)
		{
			StartRecording(hWnd);
        }
		else if (wmId == IDB_STOP)
		{
			StopRecording(hWnd);
        }
		else if (wmId == IDM_SAVEFILE)
		{
			ChangeFileLocation(hWnd);
		}
		// Checking/unchecking dynamically added gpu devices
		else if ((wmId >= ID_DEVICE_START) && (wmId <= ID_DEVICE_START + (int)s_vAdapters.size()))
		{
			const int id = wmId - ID_DEVICE_START;
			s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_ADAPTERID, id);
			UpdateMenuItems();
		}
		// Checking/unchecking dynamically added capture source monitors
		else if (wmId >= ID_CAPTURE_SOURCE_START && wmId <= ID_CAPTURE_SOURCE_START + (int)s_vDisplays.size())
		{
			// Find the id of the selected adapter in the menu
			const int id = wmId - ID_CAPTURE_SOURCE_START;
			// Find the adapter in the vector list
			unsigned adapterIdx = s_vDisplays[id].adapterIdx;
			// Set on the pipeline
			s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_MONITORID, adapterIdx);
			UpdateMenuItems();
		}
        // Checking/unchecking dynamically added capture components
        else if (wmId >= ID_CAPTURE_COMPONENT_START && wmId <= ID_CAPTURE_COMPONENT_START + 2)
        {
            const int id = wmId - ID_CAPTURE_COMPONENT_START;
            if (id == 0)
            {
                s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_CAPTURE_COMPONENT, L"AMD");
            }
            else
            {
                s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_CAPTURE_COMPONENT, L"DD");
            }
            UpdateMenuItems();
        }
		break;
	case WM_TIMER:
		UpdateFps(hWnd);
		break;
    case WM_CLOSE:
    case WM_DESTROY:
		s_pPipeline->Terminate();
		PostQuitMessage(0);
		break;
	}
	return 0;
}

//-------------------------------------------------------------------------------------------------
// Handle save file dialog
void ChangeFileLocation(HWND hWnd)
{
    // Even though we're changing it, get it here to show default in dialog
	TCHAR szDefaultFile[1024];
    GetCurrentFileLocation(szDefaultFile);

	OPENFILENAME ofn = {};
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = szDefaultFile;
	ofn.nMaxFile = sizeof(szDefaultFile) / sizeof(TCHAR);
	ofn.lpstrFilter = L"Videos\0*.WMV;*.WMA;*.AVI;*.ASF;*.FLV;*.BFI;*.CAF;*.GXF;*.IFF;*.RL2;*.MP4;*.3GP;*.QTFF;*.MKV;*.MK3D;*.MKA;*.MKS;*.MPG;*.MPEG;*.PS;*.TS;*.MXF;*.OGV;*.OGA;*.OGX;*.OGG;*.SPX;*.FLV;*.F4V;*.F4P;*.F4A;*.F4B;*.JSV;*.h264;*.264;*.vc1;*.mov;*.mvc;*.m1v;*.m2v;*.m2ts;*.vpk;*.yuv;*.rgb;*.nv12;*.h265;*.265\0All\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrInitialDir = L"%USERPROFILE%\\Videos";
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

	if (GetSaveFileName(&ofn) == TRUE)
	{
		s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, ofn.lpstrFile);
	}
}

//-------------------------------------------------------------------------------------------------
// Get file name based on system time (e.g. DVRRecording-2017-09-26-12-30-00.mp4)
void GetDirectoryFileLocation(TCHAR* szDirectory, TCHAR* szFile, bool generic)
{
	assert(szDirectory);
	assert(szFile);
	//
	SYSTEMTIME lt;
	ZeroMemory(&lt, sizeof(lt));
	if (!generic)
	{
		GetLocalTime(&lt);
	}
	swprintf_s(szFile, 1024, L"%s\\DVRRecording-%d-%02d-%02d-%02d-%02d-%02d.mp4",
		szDirectory, lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
}

//-------------------------------------------------------------------------------------------------
// Get file name based on system time (e.g. DVRRecording-2017-09-26-12-30-00.mp4)
void GetDefaultFileLocation(TCHAR* szFile, bool generic)
{
	TCHAR szDirectory[1024];
	swprintf_s(szDirectory, 1024, L"%s\\Videos", _wgetenv(L"USERPROFILE"));
	GetDirectoryFileLocation(szDirectory, szFile, generic);
}

//-------------------------------------------------------------------------------------------------
// Get current file name for OpenFile dialog
void GetCurrentFileLocation(TCHAR* szFile)
{
    GetDefaultFileLocation(szFile, true);

    std::wstring szFileOld = L"";
    s_pPipeline->GetParamWString(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szFileOld);

    // If the file location is not set, or is default, then we use the default directory
    std::wstring::size_type foundAtIndex = szFileOld.find(szFile);
    if (0 == szFileOld.size() || 0 == foundAtIndex)
    {
        // Use the default we have
    }
    else if (std::wstring::npos == foundAtIndex)
    {
        // The user has changed the default path so find the last forward or
        // backwards slash
        std::wstring::size_type slashFoundAtIndex = szFileOld.rfind(L"/");
        if (std::wstring::npos == slashFoundAtIndex)
        {
            slashFoundAtIndex = szFileOld.rfind(L"\\");
        }
        if (std::wstring::npos != slashFoundAtIndex)
        {
            std::wstring dpath = szFileOld.substr(0, slashFoundAtIndex);
            GetDirectoryFileLocation((TCHAR*)dpath.c_str(), szFile, true /* generic */);
        }
    }
}

//-------------------------------------------------------------------------------------------------
// Get record file name 
void GetRecordFileLocation(TCHAR* szFile)
{
    // Set record file location
    TCHAR szFileDefaultStart[1024];
    swprintf_s(szFileDefaultStart, 1024, L"%s\\Videos\\DVRRecording", _wgetenv(L"USERPROFILE"));

    std::wstring szFileOld = L"";
    s_pPipeline->GetParamWString(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szFileOld);
    swprintf_s(szFile, 1024, L"%s", szFileOld.c_str());

    // If the file location is not set, or is default, overwrite it with an updated default location based on date
    std::wstring::size_type foundAtIndex = szFileOld.find(szFileDefaultStart);
    if (0 == szFileOld.size() || 0 == foundAtIndex)
    {
        GetDefaultFileLocation(szFile);
    }
    else if (std::wstring::npos == foundAtIndex)
    {
        // The user has changed the default path so find the last forward or
        // backwards slash
        std::wstring::size_type slashFoundAtIndex = szFileOld.rfind(L"/");
        if (std::wstring::npos == slashFoundAtIndex)
        {
            slashFoundAtIndex = szFileOld.rfind(L"\\");
        }
        if (std::wstring::npos != slashFoundAtIndex)
        {
            std::wstring dpath = szFileOld.substr(0, slashFoundAtIndex);
            GetDirectoryFileLocation((TCHAR*)dpath.c_str(), szFile);
        }
    }
}

//-------------------------------------------------------------------------------------------------
// Handle Record button press
void StartRecording(HWND hWnd)
{


	// Set record file location
	TCHAR szFileDefaultStart[1024];
    GetRecordFileLocation(szFileDefaultStart);
    s_pPipeline->SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szFileDefaultStart);

	AMF_RESULT res = s_pPipeline->Init();
	if (res != AMF_OK)
	{
        FailedRecording(hWnd, false);
		return;
	}
    res = s_pPipeline->Start();
    if (res != AMF_OK)
    {
        FailedRecording(hWnd, false);
        return;
    }
    // Enable stop button and disable record button to prevent repeated commands
    UpdateButtons(hWnd, true);


    // initialize the timer to update FPS
    s_uiFpsTimerId = SetTimer(g_hDlg, TIMER_ID, 1000, NULL);


	// Update message
	std::wstring str = L"Recording";
#ifdef _DEBUG
	if (s_bOCLConverter)
	{
		str += L" (using OCL) ";
	}
#endif
	str += L"...";
	UpdateMessage(str.c_str());

    // also add the file name being used for recording
    std::wstring szWriteFile = L"";
    s_pPipeline->GetParamWString(DisplayDvrPipeline::PARAM_NAME_OUTPUT, szWriteFile);
    szWriteFile = L"   " + szWriteFile;
    UpdateMessage(szWriteFile.c_str());
}

//-------------------------------------------------------------------------------------------------
// Handle Stop button press
void StopRecording(HWND hWnd)
{
	// Enable record button and disable stop button to prevent repeated commands
    UpdateButtons(hWnd, false);

	// Stop recording
	s_pPipeline->Stop();

    // stop the timer
    KillTimer(g_hDlg, s_uiFpsTimerId);

	UpdateMessage(L"Recording stopped.");
    UpdateFps(hWnd);
}

//-------------------------------------------------------------------------------------------------
// If recording failed to init or to start
void FailedRecording(HWND hWnd, bool init)
{
    LOG_ERROR(((init) ? L"DisplayDvrPipeline failed to initialize"
                      : L"DisplayDvrPipeline failed to start"));

    // Get the pipeline message if there is one
    const wchar_t* pErrMsg = s_pPipeline->GetErrorMsg();
    std::wstring   initMsg = (!pErrMsg) ? L"" : pErrMsg;
    if (initMsg.empty())
    {
        initMsg = (init) ? L"Failed to initialize pipeline"
                         : L"Failed to start pipeline";
    }
    UpdateMessage(initMsg.c_str());

    // Grey out the Record and Stop buttons
    UpdateButtons(hWnd, false);

    // 
    g_AMFFactory.Terminate();
}

//-------------------------------------------------------------------------------------------------
// Update the buttons accordingly
void UpdateButtons(HWND hWnd, bool recording)
{
    // Enable stop button and disable record button to prevent repeated commands
    ::EnableWindow(::GetDlgItem(hWnd, IDB_RECORD), recording ? 0 : 1);
    ::EnableWindow(::GetDlgItem(hWnd, IDB_STOP), recording ? 1 : 0);

}

//-------------------------------------------------------------------------------------------------
// disply the recording FPS in the status bar...
void UpdateFps(HWND hWnd)
{
	if (s_pPipeline->GetState() == PipelineStateRunning)
	{
		TCHAR windowText[1000];
		double fps = s_pPipeline->GetFPS();
		swprintf_s(windowText, 1000, L"FPS: %.1f", fps);

        // display the FPS into the second portion of the status bar
        ::SendDlgItemMessage(g_hDlg, ID_STATUS_BAR, SB_SETTEXT, 1, (LPARAM) windowText);
    }
    else
    {
        // clear the FPS
        ::SendDlgItemMessage(g_hDlg, ID_STATUS_BAR, SB_SETTEXT, 1, (LPARAM) L"");
    }
}

//-------------------------------------------------------------------------------------------------
void UpdateMessage(const wchar_t *msg)
{
    if (msg)
    {
        HWND hEdit = ::GetDlgItem(g_hDlg, IDE_MESSAGES);
        int  count = ::GetWindowTextLength(hEdit);

        if (count > 0)
        {
            // append end of line
            ::SendMessage(hEdit, EM_SETSEL, (WPARAM) count, (LPARAM) count);
            ::SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM) L"\r\n");
            count += 2;
        }

        // append new text
        ::SendMessage(hEdit, EM_SETSEL, (WPARAM) count, (LPARAM) count);
        ::SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM) msg);

        // scroll to the end of the control to have the last line visible
        ::SendDlgItemMessage(g_hDlg, IDE_MESSAGES, EM_LINESCROLL, 0, 10000);
    }
}

//-------------------------------------------------------------------------------------------------
// Message handler for simple dialog boxes.
INT_PTR CALLBACK Dialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCLOSE)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

//-------------------------------------------------------------------------------------------------
// Message handler for About dialog box
INT_PTR CALLBACK AboutDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCLOSE || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}