#define WIN32_LEAN_AND_MEAN
#include <jawt_md.h>
#include <Windows.h>
#include <shellscalingapi.h>
#include <cassert>
#include <dwmapi.h>
#include <sstream>
#include "jni_helpers.h"

extern "C"
{
    JNIEXPORT void JNICALL Java_org_jetbrains_skiko_HardwareLayer_nativeInit(JNIEnv *env, jobject canvas, jlong platformInfoPtr)
    {
    }

    JNIEXPORT void JNICALL Java_org_jetbrains_skiko_HardwareLayer_nativeDispose(JNIEnv *env, jobject canvas)
    {
    }

    JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_HardwareLayer_getWindowHandle(JNIEnv *env, jobject canvas, jlong platformInfoPtr)
    {
        JAWT_Win32DrawingSurfaceInfo* dsi_win = fromJavaPointer<JAWT_Win32DrawingSurfaceInfo *>(platformInfoPtr);
        HWND ancestor = GetAncestor(dsi_win->hwnd, GA_PARENT);
        assert(ancestor != NULL);
        return (jlong) ancestor;
    }

    JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_HardwareLayer_getContentHandle(JNIEnv *env, jobject canvas, jlong platformInfoPtr)
    {
        JAWT_Win32DrawingSurfaceInfo* dsi_win = fromJavaPointer<JAWT_Win32DrawingSurfaceInfo *>(platformInfoPtr);
        return (jlong) dsi_win->hwnd;
    }

    JNIEXPORT jint JNICALL Java_org_jetbrains_skiko_SystemThemeHelper_getCurrentSystemTheme(JNIEnv *env, jobject topLevel)
    {
        auto subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
        auto name = L"AppsUseLightTheme";
        DWORD result;
        DWORD result_length = sizeof(result);
        auto status = RegGetValueW(
                HKEY_CURRENT_USER,
                subkey,
                name,
                RRF_RT_DWORD,
                NULL,
                &result,
                &result_length
        );
        switch (status) {
            case ERROR_SUCCESS:
                if (result) {
                    // Light.
                    return 0;
                } else {
                    // Dark.
                    return 1;
                }
             default:
                // Unknown.
                return 2;
         }
    }

    JNIEXPORT jint JNICALL Java_org_jetbrains_skiko_HardwareLayer_getCurrentDPI(JNIEnv *env, jobject canvas, jlong platformInfoPtr)
    {
        typedef HRESULT (STDAPICALLTYPE *GDFM)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
        static GDFM getDpiForMonitor = nullptr;
        typedef UINT (WINAPI *GDFW)(HWND);
        static GDFW getDpiForWindow = nullptr;

        // Try to dynamically load GetDpiForWindow and GetDpiForMonitor - they are only supported from Windows 10 and 8.1 respectively
        static bool dynamicFunctionsLoaded = false;
        if (!dynamicFunctionsLoaded) {
            HINSTANCE shcoreDll = LoadLibrary("Shcore.dll");
            if (shcoreDll) {
                getDpiForMonitor = reinterpret_cast<GDFM>(GetProcAddress(shcoreDll, "GetDpiForMonitor"));
            }
            
            HINSTANCE user32Dll = LoadLibrary("User32");
            if (user32Dll) {
                getDpiForWindow = reinterpret_cast<GDFW>(GetProcAddress(user32Dll, "GetDpiForWindow"));
            }
            
            dynamicFunctionsLoaded = true;
        }

        HWND hwnd = (HWND)Java_org_jetbrains_skiko_HardwareLayer_getWindowHandle(env, canvas, platformInfoPtr);
        int dpi = 0;
        if (getDpiForMonitor) {
            HMONITOR display = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            UINT xDpi = 0, yDpi = 0;
            getDpiForMonitor(display, MDT_RAW_DPI, &xDpi, &yDpi);
            dpi = (int) xDpi;
        }
        
        // We can get dpi:0 if we set up multiple displays for content duplication (mirror). 
        if (dpi == 0) {            
            if (getDpiForWindow) {
                // get default system dpi
                dpi = getDpiForWindow(hwnd);
            }
        }
        
        if (dpi == 0) {
            // If failed to get DPI, assume standard 96
            dpi = 96;
        }
        
        return dpi;
    }

    static JavaVM* vmGlobal = nullptr;

    static JNIEnv* GetJNIEnv()
    {
        JNIEnv* env = nullptr;
        if (vmGlobal->GetEnv((void**)&env, JNI_VERSION_10) == JNI_OK)
        {
            return env;
        }
        return nullptr;
    }

    static void JPrintln(const char* message) {
        JNIEnv* env = GetJNIEnv();
        // 查找 System 类
        jclass systemClass = (*env).FindClass("java/lang/System");
        if (systemClass == NULL) {
            return;
        }

        // 查找 System 类中的 out 字段
        jfieldID outField = (*env).GetStaticFieldID(systemClass, "out", "Ljava/io/PrintStream;");
        if (outField == NULL) {
            return;
        }

        // 获取 System.out 对象
        jobject outObject = (*env).GetStaticObjectField(systemClass, outField);
        if (outObject == NULL) {
            return;
        }

        // 查找 PrintStream 类
        jclass printStreamClass = (*env).FindClass("java/io/PrintStream");
        if (printStreamClass == NULL) {
            return;
        }

        // 查找 PrintStream 类中的 println 方法
        jmethodID printlnMethod = (*env).GetMethodID(printStreamClass, "println", "(Ljava/lang/String;)V");
        if (printlnMethod == NULL) {
            return;
        }

        // 调用 System.out.println 方法
        (*env).CallVoidMethod(outObject, printlnMethod, (*env).NewStringUTF(message));
    }

    static void UpdateMenuItemInfo(HMENU menu, MENUITEMINFO* menuItemInfo, UINT item, bool enabled)
    {
        (*menuItemInfo).fState = enabled ? MF_ENABLED : MF_DISABLED;
        SetMenuItemInfo(menu, item, FALSE, menuItemInfo);
    }

    static LRESULT call_default_window_proc(HWND handle, UINT message, WPARAM w_param, LPARAM l_param)
    {
        const TCHAR* DEF_WND_PROC_PROP = __TEXT("default_window_proc");
        WNDPROC def_window_proc = (WNDPROC)GetProp(handle, DEF_WND_PROC_PROP);
        return CallWindowProc(def_window_proc, handle, message, w_param, l_param);
    }

    static LRESULT WindowNCRButtonUpProc(HWND handle, UINT message, WPARAM w_param, LPARAM l_param)
    {
        int oldStyle = GetWindowLong(handle, GWL_STYLE);
        SetWindowLong(handle, GWL_STYLE, oldStyle | WS_SYSMENU);
        HMENU menu = GetSystemMenu(handle, FALSE);
        SetWindowLong(handle, GWL_STYLE, oldStyle);
        WINDOWPLACEMENT placement;
        if (!GetWindowPlacement(handle, &placement))
        {
            return call_default_window_proc(handle, message, w_param, l_param);
        }
        if (menu)
        {
            bool isMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
            MENUITEMINFO menuItemInfo;
            menuItemInfo.cbSize = sizeof(MENUITEMINFO);
            menuItemInfo.fMask = MIIM_STATE;
            menuItemInfo.fType = MFT_STRING;
            UpdateMenuItemInfo(menu, &menuItemInfo, SC_RESTORE, isMaximized);
            UpdateMenuItemInfo(menu, &menuItemInfo, SC_MOVE, !isMaximized);
            UpdateMenuItemInfo(menu, &menuItemInfo, SC_SIZE, !isMaximized);
            UpdateMenuItemInfo(menu, &menuItemInfo, SC_MINIMIZE, true);
            UpdateMenuItemInfo(menu, &menuItemInfo, SC_MAXIMIZE, !isMaximized);
            UpdateMenuItemInfo(menu, &menuItemInfo, SC_CLOSE, true);
            SetMenuDefaultItem(menu, WINT_MAX, FALSE);
            int x = LOWORD(l_param);
            int y = HIWORD(l_param);

            LRESULT ret = TrackPopupMenu(menu, TPM_RETURNCMD, x, y, 0, handle, nullptr);
            if (ret != 0)
            {
                PostMessage(handle, WM_SYSCOMMAND, ret, 0);
            }
        }
        return call_default_window_proc(handle, message, w_param, l_param);
    }

    static void set_title_bar_height(HWND handle, float title_bar_height)
    {
        const TCHAR* TITLEBAR_HEIGHT_PROP = __TEXT("titlebar_height");
        float* titlebar_height = (float*)GetProp(handle, TITLEBAR_HEIGHT_PROP);
        if (titlebar_height != NULL)
        {
            *titlebar_height = title_bar_height;
        }
        else
        {
            titlebar_height = (float*)malloc(sizeof(float));
            *titlebar_height = title_bar_height;
            SetProp(handle, TITLEBAR_HEIGHT_PROP, titlebar_height);
        }
    }

    static int get_title_bar_height_for_dpi(HWND handle)
    {
        //return 300;
        const TCHAR* TITLEBAR_HEIGHT_PROP = __TEXT("titlebar_height");
        float* titlebar_height = (float*)GetProp(handle, TITLEBAR_HEIGHT_PROP);
        if (titlebar_height == NULL)
        {
            return 0;
        }
        UINT dpi = GetDpiForWindow(handle);

        return MulDiv(*titlebar_height, dpi, 96);
    }

    static BOOL set_window_proc(HWND handle, WNDPROC proc)
    {

        WNDPROC old_window_proc = (WNDPROC)GetWindowLongPtr(handle, GWLP_WNDPROC);
        if (old_window_proc != proc)
        {
            const TCHAR* DEF_WINDOW_PROC_PROP = __TEXT("default_window_proc");
            SetProp(handle, DEF_WINDOW_PROC_PROP, (HANDLE)old_window_proc);
            SetWindowLongPtr(handle, GWLP_WNDPROC, (LONG_PTR)proc);
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }

    static LRESULT skia_nc_hit_test(HWND handle, UINT message, WPARAM w_param, LPARAM l_param)
    {
        int x = LOWORD(l_param);
        int y = HIWORD(l_param);
        POINT point = { x, y };
        ScreenToClient(handle, &point);
        int result = HTCLIENT;
        HWND ancestor = GetAncestor(handle, GA_PARENT);
        JPrintln("skia hit test");
        if (ancestor != NULL)
        {
            JPrintln("parent not null");
            std::stringstream test;
            test << "point y is:" << point.y << std::endl;
            //std::string result = "pointer y" + std::to_string(point.y);
            JPrintln(test.str().c_str());
            if (point.y <= get_title_bar_height_for_dpi(handle))
                    {
                        return LRESULT(HTTRANSPARENT);
                    }
           LRESULT ret = PostMessage(ancestor, message, w_param, l_param);
           switch (ret)
           {
           case HTCLIENT:
               return call_default_window_proc(handle, message, w_param, l_param);
               break;
           default:
               return HTTRANSPARENT;
               break;
           }
        }

        return call_default_window_proc(handle, message, w_param, l_param);
    }

    static LRESULT skia_window_proc(HWND handle, UINT message, WPARAM w_param, LPARAM l_param)
    {
        JPrintln("test skia print");
        switch (message)
        {
            case WM_NCHITTEST:
                return skia_nc_hit_test(handle, message, w_param, l_param);
                break;
            default:
                return call_default_window_proc(handle, message, w_param, l_param);
            break;
        }
    }

    static LRESULT window_nc_calc_size(HWND handle, UINT message, WPARAM w_param, LPARAM l_param)
    {
        if (!w_param) return call_default_window_proc(handle, message, w_param, l_param);
        NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)l_param;
        const int old_top = params->rgrc[0].top;
        const RECT old_size = params->rgrc[0];
//        LRESULT result = DefWindowProc(handle, message, w_param, l_param);
//        if ((int) result != 0)
//        {
//            return result;
//        }
        RECT new_size = params->rgrc[0];
        new_size.top = old_top;

        UINT dpi = GetDpiForWindow(handle);
        int frame_x = GetSystemMetricsForDpi(SM_CXFRAME, dpi);
        int frame_y = GetSystemMetricsForDpi(SM_CYFRAME, dpi);
        int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);

        new_size.top += 1;
        new_size.right -= frame_x + padding;
        new_size.left += frame_x + padding;
        new_size.bottom -= frame_y + padding;

        params->rgrc[0] = new_size;
        return LRESULT(0);
    }

    static LRESULT custom_title_bar_window_proc_hit_test(HWND handle, UINT message, WPARAM w_param, LPARAM l_param)
    {
        int x = LOWORD(l_param);
        int y = HIWORD(l_param);
        POINT point = { x, y };
        ScreenToClient(handle, &point);
        LRESULT ret = call_default_window_proc(handle, message, w_param, l_param);
        JPrintln("window hit test proc");
        switch (ret)
        {
        case HTBOTTOM:
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
        case HTLEFT:
        case HTRIGHT:
        case HTTOP:
        case HTTOPLEFT:
        case HTTOPRIGHT:
            return ret;
            break;
        default:
//            return ret;
            //return LRESULT(HTCLIENT);
            if (point.y <= get_title_bar_height_for_dpi(handle))
            {
                return LRESULT(HTCAPTION);
            }
            else
            {
                return LRESULT(HTCLIENT);
            }
            break;
        }
        return ret;
    }

    static LRESULT custom_title_bar_window_proc(HWND handle, UINT message, WPARAM w_param, LPARAM l_param)
    {
 /*       char buffer[100];
        snprintf(buffer, sizeof(buffer), "proc call: message: %d", (int) message);
        JPrintln(buffer);*/
        //JPrintln("window proc message");
        switch (message)
        {
            case WM_NCCALCSIZE:
                return window_nc_calc_size(handle, message, w_param, l_param);
                break;
            case WM_NCHITTEST:
                return custom_title_bar_window_proc_hit_test(handle, message, w_param, l_param);
                break;

            case WM_NCRBUTTONUP:
                return WindowNCRButtonUpProc(handle, message, w_param, l_param);
                break;
            default:
                return call_default_window_proc(handle, message, w_param, l_param);
                break;
        }
    }

    JNIEXPORT void JNICALL Java_org_jetbrains_skiko_PlatformOperationsKt_windowsDisableTitleBar(JNIEnv* env, jobject properties, jobject canvas, jlong platformInfoPtr, jfloat customHeaderHeight)
    {
        HWND windowHWND = (HWND)Java_org_jetbrains_skiko_HardwareLayer_getWindowHandle(env, canvas, platformInfoPtr);
        env->GetJavaVM(&vmGlobal);
        JPrintln("native window proc");
        if (windowHWND == NULL) return;
        JPrintln("hwnd not null");

        set_title_bar_height(windowHWND, customHeaderHeight);
        if (set_window_proc(windowHWND, custom_title_bar_window_proc))
        {
            //Apply window_proc
            int style = GetWindowLong(windowHWND, GWL_STYLE);
            style = style | WS_CAPTION;
            style = style & ~WS_SYSMENU;
            SetWindowLong(windowHWND, GWL_STYLE, style);

            //MARGINS window_margins = { 0, 0, 1, 0 };
            MARGINS window_margins = { 0, 0, -1, -1 };
            DwmExtendFrameIntoClientArea(windowHWND, &window_margins);
            int value = DWM_SYSTEMBACKDROP_TYPE::DWMSBT_MAINWINDOW;
            DwmSetWindowAttribute(windowHWND, DWMWA_SYSTEMBACKDROP_TYPE, &value, sizeof(value));
            BOOL backdrop = TRUE;
            DwmSetWindowAttribute(windowHWND, DWMWA_USE_HOSTBACKDROPBRUSH, &backdrop, sizeof(backdrop));
            int color = DWMWA_COLOR_NONE;
            DwmSetWindowAttribute(windowHWND, DWMWA_CAPTION_COLOR, &color, sizeof(color));
            SetWindowPos(windowHWND, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

            JPrintln("set window proc");
        }

        HWND contentHWND = (HWND)Java_org_jetbrains_skiko_HardwareLayer_getContentHandle(env, canvas, platformInfoPtr);
        if (contentHWND != NULL)
        {
            set_title_bar_height(contentHWND, customHeaderHeight);
            set_window_proc(contentHWND, skia_window_proc);
            JPrintln("set content proc");
        }
        
        
    }
} // extern "C"
