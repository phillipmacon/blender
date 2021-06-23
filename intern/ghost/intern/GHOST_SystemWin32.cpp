/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file
 * \ingroup GHOST
 */

#include "GHOST_SystemWin32.h"
#include "GHOST_ContextD3D.h"
#include "GHOST_EventDragnDrop.h"

#ifndef _WIN32_IE
#  define _WIN32_IE 0x0501 /* shipped before XP, so doesn't impose additional requirements */
#endif

#include <commctrl.h>
#include <psapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <windowsx.h>

#include "utf_winfunc.h"
#include "utfconv.h"

#include "GHOST_DisplayManagerWin32.h"
#include "GHOST_EventButton.h"
#include "GHOST_EventCursor.h"
#include "GHOST_EventKey.h"
#include "GHOST_EventWheel.h"
#include "GHOST_TimerManager.h"
#include "GHOST_TimerTask.h"
#include "GHOST_WindowManager.h"
#include "GHOST_WindowWin32.h"

#if defined(WITH_GL_EGL)
#  include "GHOST_ContextEGL.h"
#else
#  include "GHOST_ContextWGL.h"
#endif

#ifdef WITH_INPUT_NDOF
#  include "GHOST_NDOFManagerWin32.h"
#endif

// Key code values not found in winuser.h
#ifndef VK_MINUS
#  define VK_MINUS 0xBD
#endif  // VK_MINUS
#ifndef VK_SEMICOLON
#  define VK_SEMICOLON 0xBA
#endif  // VK_SEMICOLON
#ifndef VK_PERIOD
#  define VK_PERIOD 0xBE
#endif  // VK_PERIOD
#ifndef VK_COMMA
#  define VK_COMMA 0xBC
#endif  // VK_COMMA
#ifndef VK_QUOTE
#  define VK_QUOTE 0xDE
#endif  // VK_QUOTE
#ifndef VK_BACK_QUOTE
#  define VK_BACK_QUOTE 0xC0
#endif  // VK_BACK_QUOTE
#ifndef VK_SLASH
#  define VK_SLASH 0xBF
#endif  // VK_SLASH
#ifndef VK_BACK_SLASH
#  define VK_BACK_SLASH 0xDC
#endif  // VK_BACK_SLASH
#ifndef VK_EQUALS
#  define VK_EQUALS 0xBB
#endif  // VK_EQUALS
#ifndef VK_OPEN_BRACKET
#  define VK_OPEN_BRACKET 0xDB
#endif  // VK_OPEN_BRACKET
#ifndef VK_CLOSE_BRACKET
#  define VK_CLOSE_BRACKET 0xDD
#endif  // VK_CLOSE_BRACKET
#ifndef VK_GR_LESS
#  define VK_GR_LESS 0xE2
#endif  // VK_GR_LESS

/* Workaround for some laptop touchpads, some of which seems to
 * have driver issues which makes it so window function receives
 * the message, but PeekMessage doesn't pick those messages for
 * some reason.
 *
 * We send a dummy WM_USER message to force PeekMessage to receive
 * something, making it so blender's window manager sees the new
 * messages coming in.
 */
#define BROKEN_PEEK_TOUCHPAD

static void initRawInput()
{
#ifdef WITH_INPUT_NDOF
#  define DEVICE_COUNT 2
#else
#  define DEVICE_COUNT 1
#endif

  RAWINPUTDEVICE devices[DEVICE_COUNT];
  memset(devices, 0, DEVICE_COUNT * sizeof(RAWINPUTDEVICE));

  // Initiates WM_INPUT messages from keyboard
  // That way GHOST can retrieve true keys
  devices[0].usUsagePage = 0x01;
  devices[0].usUsage = 0x06; /* http://msdn.microsoft.com/en-us/windows/hardware/gg487473.aspx */

#ifdef WITH_INPUT_NDOF
  // multi-axis mouse (SpaceNavigator, etc.)
  devices[1].usUsagePage = 0x01;
  devices[1].usUsage = 0x08;
#endif

  if (RegisterRawInputDevices(devices, DEVICE_COUNT, sizeof(RAWINPUTDEVICE)))
    ;  // yay!
  else
    GHOST_PRINTF("could not register for RawInput: %d\n", (int)GetLastError());

#undef DEVICE_COUNT
}

typedef BOOL(API *GHOST_WIN32_EnableNonClientDpiScaling)(HWND);

GHOST_SystemWin32::GHOST_SystemWin32()
    : m_hasPerformanceCounter(false), m_freq(0), m_start(0), m_lfstart(0)
{
  m_displayManager = new GHOST_DisplayManagerWin32();
  GHOST_ASSERT(m_displayManager, "GHOST_SystemWin32::GHOST_SystemWin32(): m_displayManager==0\n");
  m_displayManager->initialize();

  m_consoleStatus = 1;

  // Tell Windows we are per monitor DPI aware. This disables the default
  // blurry scaling and enables WM_DPICHANGED to allow us to draw at proper DPI.
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  // Check if current keyboard layout uses AltGr and save keylayout ID for
  // specialized handling if keys like VK_OEM_*. I.e. french keylayout
  // generates VK_OEM_8 for their exclamation key (key left of right shift)
  this->handleKeyboardChange();
  // Require COM for GHOST_DropTargetWin32 created in GHOST_WindowWin32.
  OleInitialize(0);

#ifdef WITH_INPUT_NDOF
  m_ndofManager = new GHOST_NDOFManagerWin32(*this);
#endif
}

GHOST_SystemWin32::~GHOST_SystemWin32()
{
  // Shutdown COM
  OleUninitialize();
  toggleConsole(1);
}

GHOST_TUns64 GHOST_SystemWin32::performanceCounterToMillis(__int64 perf_ticks) const
{
  // Calculate the time passed since system initialization.
  __int64 delta = (perf_ticks - m_start) * 1000;

  GHOST_TUns64 t = (GHOST_TUns64)(delta / m_freq);
  return t;
}

GHOST_TUns64 GHOST_SystemWin32::tickCountToMillis(__int64 ticks) const
{
  return ticks - m_lfstart;
}

GHOST_TUns64 GHOST_SystemWin32::getMilliSeconds() const
{
  // Hardware does not support high resolution timers. We will use GetTickCount instead then.
  if (!m_hasPerformanceCounter) {
    return tickCountToMillis(::GetTickCount());
  }

  // Retrieve current count
  __int64 count = 0;
  ::QueryPerformanceCounter((LARGE_INTEGER *)&count);

  return performanceCounterToMillis(count);
}

GHOST_TUns8 GHOST_SystemWin32::getNumDisplays() const
{
  GHOST_ASSERT(m_displayManager, "GHOST_SystemWin32::getNumDisplays(): m_displayManager==0\n");
  GHOST_TUns8 numDisplays;
  m_displayManager->getNumDisplays(numDisplays);
  return numDisplays;
}

void GHOST_SystemWin32::getMainDisplayDimensions(GHOST_TUns32 &width, GHOST_TUns32 &height) const
{
  width = ::GetSystemMetrics(SM_CXSCREEN);
  height = ::GetSystemMetrics(SM_CYSCREEN);
}

void GHOST_SystemWin32::getAllDisplayDimensions(GHOST_TUns32 &width, GHOST_TUns32 &height) const
{
  width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
  height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

GHOST_IWindow *GHOST_SystemWin32::createWindow(const char *title,
                                               GHOST_TInt32 left,
                                               GHOST_TInt32 top,
                                               GHOST_TUns32 width,
                                               GHOST_TUns32 height,
                                               GHOST_TWindowState state,
                                               GHOST_TDrawingContextType type,
                                               GHOST_GLSettings glSettings,
                                               const bool exclusive,
                                               const bool is_dialog,
                                               const GHOST_IWindow *parentWindow)
{
  GHOST_WindowWin32 *window = new GHOST_WindowWin32(
      this,
      title,
      left,
      top,
      width,
      height,
      state,
      type,
      ((glSettings.flags & GHOST_glStereoVisual) != 0),
      ((glSettings.flags & GHOST_glAlphaBackground) != 0),
      (GHOST_WindowWin32 *)parentWindow,
      ((glSettings.flags & GHOST_glDebugContext) != 0),
      is_dialog);

  if (window->getValid()) {
    // Store the pointer to the window
    m_windowManager->addWindow(window);
    m_windowManager->setActiveWindow(window);
  }
  else {
    GHOST_PRINT("GHOST_SystemWin32::createWindow(): window invalid\n");
    delete window;
    window = NULL;
  }

  return window;
}

/**
 * Create a new offscreen context.
 * Never explicitly delete the window, use #disposeContext() instead.
 * \return The new context (or 0 if creation failed).
 */
GHOST_IContext *GHOST_SystemWin32::createOffscreenContext(GHOST_GLSettings glSettings)
{
  const bool debug_context = (glSettings.flags & GHOST_glDebugContext) != 0;

  GHOST_Context *context;

  HWND wnd = CreateWindowA("STATIC",
                           "BlenderGLEW",
                           WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                           0,
                           0,
                           64,
                           64,
                           NULL,
                           NULL,
                           GetModuleHandle(NULL),
                           NULL);

  HDC mHDC = GetDC(wnd);
  HDC prev_hdc = wglGetCurrentDC();
  HGLRC prev_context = wglGetCurrentContext();
#if defined(WITH_GL_PROFILE_CORE)
  for (int minor = 5; minor >= 0; --minor) {
    context = new GHOST_ContextWGL(false,
                                   true,
                                   wnd,
                                   mHDC,
                                   WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                                   4,
                                   minor,
                                   (debug_context ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
                                   GHOST_OPENGL_WGL_RESET_NOTIFICATION_STRATEGY);

    if (context->initializeDrawingContext()) {
      goto finished;
    }
    else {
      delete context;
    }
  }

  context = new GHOST_ContextWGL(false,
                                 true,
                                 wnd,
                                 mHDC,
                                 WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                                 3,
                                 3,
                                 (debug_context ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
                                 GHOST_OPENGL_WGL_RESET_NOTIFICATION_STRATEGY);

  if (context->initializeDrawingContext()) {
    goto finished;
  }
  else {
    delete context;
    return NULL;
  }

#elif defined(WITH_GL_PROFILE_COMPAT)
  // ask for 2.1 context, driver gives any GL version >= 2.1
  // (hopefully the latest compatibility profile)
  // 2.1 ignores the profile bit & is incompatible with core profile
  context = new GHOST_ContextWGL(false,
                                 true,
                                 NULL,
                                 NULL,
                                 0,  // no profile bit
                                 2,
                                 1,
                                 (debug_context ? WGL_CONTEXT_DEBUG_BIT_ARB : 0),
                                 GHOST_OPENGL_WGL_RESET_NOTIFICATION_STRATEGY);

  if (context->initializeDrawingContext()) {
    return context;
  }
  else {
    delete context;
  }
#else
#  error  // must specify either core or compat at build time
#endif
finished:
  wglMakeCurrent(prev_hdc, prev_context);
  return context;
}

/**
 * Dispose of a context.
 * \param context: Pointer to the context to be disposed.
 * \return Indication of success.
 */
GHOST_TSuccess GHOST_SystemWin32::disposeContext(GHOST_IContext *context)
{
  delete context;

  return GHOST_kSuccess;
}

/**
 * Create a new offscreen DirectX 11 context.
 * Never explicitly delete the window, use #disposeContext() instead.
 * \return The new context (or 0 if creation failed).
 */
GHOST_ContextD3D *GHOST_SystemWin32::createOffscreenContextD3D()
{
  GHOST_ContextD3D *context;

  HWND wnd = CreateWindowA("STATIC",
                           "Blender XR",
                           WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                           0,
                           0,
                           64,
                           64,
                           NULL,
                           NULL,
                           GetModuleHandle(NULL),
                           NULL);

  context = new GHOST_ContextD3D(false, wnd);
  if (context->initializeDrawingContext() == GHOST_kFailure) {
    delete context;
  }

  return context;
}

GHOST_TSuccess GHOST_SystemWin32::disposeContextD3D(GHOST_ContextD3D *context)
{
  delete context;

  return GHOST_kSuccess;
}

bool GHOST_SystemWin32::processEvents(bool waitForEvent)
{
  MSG msg;
  bool hasEventHandled = false;

  do {
    GHOST_TimerManager *timerMgr = getTimerManager();

    if (waitForEvent && !::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
#if 1
      ::Sleep(1);
#else
      GHOST_TUns64 next = timerMgr->nextFireTime();
      GHOST_TInt64 maxSleep = next - getMilliSeconds();

      if (next == GHOST_kFireTimeNever) {
        ::WaitMessage();
      }
      else if (maxSleep >= 0.0) {
        ::SetTimer(NULL, 0, maxSleep, NULL);
        ::WaitMessage();
        ::KillTimer(NULL, 0);
      }
#endif
    }

    if (timerMgr->fireTimers(getMilliSeconds())) {
      hasEventHandled = true;
    }

    // Process all the events waiting for us
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE) != 0) {
      // TranslateMessage doesn't alter the message, and doesn't change our raw keyboard data.
      // Needed for MapVirtualKey or if we ever need to get chars from wm_ime_char or similar.
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
      hasEventHandled = true;
    }

    /* PeekMessage above is allowed to dispatch messages to the wndproc without us
     * noticing, so we need to check the event manager here to see if there are
     * events waiting in the queue.
     */
    hasEventHandled |= this->m_eventManager->getNumEvents() > 0;

  } while (waitForEvent && !hasEventHandled);

  return hasEventHandled;
}

GHOST_TSuccess GHOST_SystemWin32::getCursorPosition(GHOST_TInt32 &x, GHOST_TInt32 &y) const
{
  POINT point;
  if (::GetCursorPos(&point)) {
    x = point.x;
    y = point.y;
    return GHOST_kSuccess;
  }
  return GHOST_kFailure;
}

GHOST_TSuccess GHOST_SystemWin32::setCursorPosition(GHOST_TInt32 x, GHOST_TInt32 y)
{
  if (!::GetActiveWindow())
    return GHOST_kFailure;
  return ::SetCursorPos(x, y) == TRUE ? GHOST_kSuccess : GHOST_kFailure;
}

GHOST_TSuccess GHOST_SystemWin32::getModifierKeys(GHOST_ModifierKeys &keys) const
{
  bool down = HIBYTE(::GetKeyState(VK_LSHIFT)) != 0;
  keys.set(GHOST_kModifierKeyLeftShift, down);
  down = HIBYTE(::GetKeyState(VK_RSHIFT)) != 0;
  keys.set(GHOST_kModifierKeyRightShift, down);

  down = HIBYTE(::GetKeyState(VK_LMENU)) != 0;
  keys.set(GHOST_kModifierKeyLeftAlt, down);
  down = HIBYTE(::GetKeyState(VK_RMENU)) != 0;
  keys.set(GHOST_kModifierKeyRightAlt, down);

  down = HIBYTE(::GetKeyState(VK_LCONTROL)) != 0;
  keys.set(GHOST_kModifierKeyLeftControl, down);
  down = HIBYTE(::GetKeyState(VK_RCONTROL)) != 0;
  keys.set(GHOST_kModifierKeyRightControl, down);

  bool lwindown = HIBYTE(::GetKeyState(VK_LWIN)) != 0;
  bool rwindown = HIBYTE(::GetKeyState(VK_RWIN)) != 0;
  if (lwindown || rwindown)
    keys.set(GHOST_kModifierKeyOS, true);
  else
    keys.set(GHOST_kModifierKeyOS, false);
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_SystemWin32::getButtons(GHOST_Buttons &buttons) const
{
  /* Check for swapped buttons (left-handed mouse buttons)
   * GetAsyncKeyState() will give back the state of the physical mouse buttons.
   */
  bool swapped = ::GetSystemMetrics(SM_SWAPBUTTON) == TRUE;

  bool down = HIBYTE(::GetAsyncKeyState(VK_LBUTTON)) != 0;
  buttons.set(swapped ? GHOST_kButtonMaskRight : GHOST_kButtonMaskLeft, down);

  down = HIBYTE(::GetAsyncKeyState(VK_MBUTTON)) != 0;
  buttons.set(GHOST_kButtonMaskMiddle, down);

  down = HIBYTE(::GetAsyncKeyState(VK_RBUTTON)) != 0;
  buttons.set(swapped ? GHOST_kButtonMaskLeft : GHOST_kButtonMaskRight, down);
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_SystemWin32::init()
{
  GHOST_TSuccess success = GHOST_System::init();
  InitCommonControls();

  /* Disable scaling on high DPI displays on Vista */
  SetProcessDPIAware();
  initRawInput();

  m_lfstart = ::GetTickCount();
  // Determine whether this system has a high frequency performance counter. */
  m_hasPerformanceCounter = ::QueryPerformanceFrequency((LARGE_INTEGER *)&m_freq) == TRUE;
  if (m_hasPerformanceCounter) {
    GHOST_PRINT("GHOST_SystemWin32::init: High Frequency Performance Timer available\n");
    ::QueryPerformanceCounter((LARGE_INTEGER *)&m_start);
  }
  else {
    GHOST_PRINT("GHOST_SystemWin32::init: High Frequency Performance Timer not available\n");
  }

  if (success) {
    WNDCLASSW wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = s_wndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = ::GetModuleHandle(0);
    wc.hIcon = ::LoadIcon(wc.hInstance, "APPICON");

    if (!wc.hIcon) {
      ::LoadIcon(NULL, IDI_APPLICATION);
    }
    wc.hCursor = ::LoadCursor(0, IDC_ARROW);
    wc.hbrBackground =
#ifdef INW32_COMPISITING
        (HBRUSH)CreateSolidBrush
#endif
        (0x00000000);
    wc.lpszMenuName = 0;
    wc.lpszClassName = L"GHOST_WindowClass";

    // Use RegisterClassEx for setting small icon
    if (::RegisterClassW(&wc) == 0) {
      success = GHOST_kFailure;
    }
  }

  return success;
}

GHOST_TSuccess GHOST_SystemWin32::exit()
{
  return GHOST_System::exit();
}

GHOST_TKey GHOST_SystemWin32::hardKey(RAWINPUT const &raw,
                                      bool *r_keyDown,
                                      bool *r_is_repeated_modifier)
{
  bool is_repeated_modifier = false;

  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();
  GHOST_TKey key = GHOST_kKeyUnknown;
  GHOST_ModifierKeys modifiers;
  system->retrieveModifierKeys(modifiers);

  // RI_KEY_BREAK doesn't work for sticky keys release, so we also
  // check for the up message
  unsigned int msg = raw.data.keyboard.Message;
  *r_keyDown = !(raw.data.keyboard.Flags & RI_KEY_BREAK) && msg != WM_KEYUP && msg != WM_SYSKEYUP;

  key = this->convertKey(raw.data.keyboard.VKey,
                         raw.data.keyboard.MakeCode,
                         (raw.data.keyboard.Flags & (RI_KEY_E1 | RI_KEY_E0)));

  // extra handling of modifier keys: don't send repeats out from GHOST
  if (key >= GHOST_kKeyLeftShift && key <= GHOST_kKeyRightAlt) {
    bool changed = false;
    GHOST_TModifierKeyMask modifier;
    switch (key) {
      case GHOST_kKeyLeftShift: {
        changed = (modifiers.get(GHOST_kModifierKeyLeftShift) != *r_keyDown);
        modifier = GHOST_kModifierKeyLeftShift;
        break;
      }
      case GHOST_kKeyRightShift: {
        changed = (modifiers.get(GHOST_kModifierKeyRightShift) != *r_keyDown);
        modifier = GHOST_kModifierKeyRightShift;
        break;
      }
      case GHOST_kKeyLeftControl: {
        changed = (modifiers.get(GHOST_kModifierKeyLeftControl) != *r_keyDown);
        modifier = GHOST_kModifierKeyLeftControl;
        break;
      }
      case GHOST_kKeyRightControl: {
        changed = (modifiers.get(GHOST_kModifierKeyRightControl) != *r_keyDown);
        modifier = GHOST_kModifierKeyRightControl;
        break;
      }
      case GHOST_kKeyLeftAlt: {
        changed = (modifiers.get(GHOST_kModifierKeyLeftAlt) != *r_keyDown);
        modifier = GHOST_kModifierKeyLeftAlt;
        break;
      }
      case GHOST_kKeyRightAlt: {
        changed = (modifiers.get(GHOST_kModifierKeyRightAlt) != *r_keyDown);
        modifier = GHOST_kModifierKeyRightAlt;
        break;
      }
      default:
        break;
    }

    if (changed) {
      modifiers.set(modifier, *r_keyDown);
      system->storeModifierKeys(modifiers);
    }
    else {
      is_repeated_modifier = true;
    }
  }

  *r_is_repeated_modifier = is_repeated_modifier;
  return key;
}

/**
 * \note this function can be extended to include other exotic cases as they arise.
 *
 * This function was added in response to bug T25715.
 * This is going to be a long list T42426.
 */
GHOST_TKey GHOST_SystemWin32::processSpecialKey(short vKey, short scanCode) const
{
  GHOST_TKey key = GHOST_kKeyUnknown;
  switch (PRIMARYLANGID(m_langId)) {
    case LANG_FRENCH:
      if (vKey == VK_OEM_8)
        key = GHOST_kKeyF13;  // oem key; used purely for shortcuts .
      break;
    case LANG_ENGLISH:
      if (SUBLANGID(m_langId) == SUBLANG_ENGLISH_UK && vKey == VK_OEM_8)  // "`¬"
        key = GHOST_kKeyAccentGrave;
      break;
  }

  return key;
}

GHOST_TKey GHOST_SystemWin32::convertKey(short vKey, short scanCode, short extend) const
{
  GHOST_TKey key;

  if ((vKey >= '0') && (vKey <= '9')) {
    // VK_0 thru VK_9 are the same as ASCII '0' thru '9' (0x30 - 0x39)
    key = (GHOST_TKey)(vKey - '0' + GHOST_kKey0);
  }
  else if ((vKey >= 'A') && (vKey <= 'Z')) {
    // VK_A thru VK_Z are the same as ASCII 'A' thru 'Z' (0x41 - 0x5A)
    key = (GHOST_TKey)(vKey - 'A' + GHOST_kKeyA);
  }
  else if ((vKey >= VK_F1) && (vKey <= VK_F24)) {
    key = (GHOST_TKey)(vKey - VK_F1 + GHOST_kKeyF1);
  }
  else {
    switch (vKey) {
      case VK_RETURN:
        key = (extend) ? GHOST_kKeyNumpadEnter : GHOST_kKeyEnter;
        break;

      case VK_BACK:
        key = GHOST_kKeyBackSpace;
        break;
      case VK_TAB:
        key = GHOST_kKeyTab;
        break;
      case VK_ESCAPE:
        key = GHOST_kKeyEsc;
        break;
      case VK_SPACE:
        key = GHOST_kKeySpace;
        break;

      case VK_INSERT:
      case VK_NUMPAD0:
        key = (extend) ? GHOST_kKeyInsert : GHOST_kKeyNumpad0;
        break;
      case VK_END:
      case VK_NUMPAD1:
        key = (extend) ? GHOST_kKeyEnd : GHOST_kKeyNumpad1;
        break;
      case VK_DOWN:
      case VK_NUMPAD2:
        key = (extend) ? GHOST_kKeyDownArrow : GHOST_kKeyNumpad2;
        break;
      case VK_NEXT:
      case VK_NUMPAD3:
        key = (extend) ? GHOST_kKeyDownPage : GHOST_kKeyNumpad3;
        break;
      case VK_LEFT:
      case VK_NUMPAD4:
        key = (extend) ? GHOST_kKeyLeftArrow : GHOST_kKeyNumpad4;
        break;
      case VK_CLEAR:
      case VK_NUMPAD5:
        key = (extend) ? GHOST_kKeyUnknown : GHOST_kKeyNumpad5;
        break;
      case VK_RIGHT:
      case VK_NUMPAD6:
        key = (extend) ? GHOST_kKeyRightArrow : GHOST_kKeyNumpad6;
        break;
      case VK_HOME:
      case VK_NUMPAD7:
        key = (extend) ? GHOST_kKeyHome : GHOST_kKeyNumpad7;
        break;
      case VK_UP:
      case VK_NUMPAD8:
        key = (extend) ? GHOST_kKeyUpArrow : GHOST_kKeyNumpad8;
        break;
      case VK_PRIOR:
      case VK_NUMPAD9:
        key = (extend) ? GHOST_kKeyUpPage : GHOST_kKeyNumpad9;
        break;
      case VK_DECIMAL:
      case VK_DELETE:
        key = (extend) ? GHOST_kKeyDelete : GHOST_kKeyNumpadPeriod;
        break;

      case VK_SNAPSHOT:
        key = GHOST_kKeyPrintScreen;
        break;
      case VK_PAUSE:
        key = GHOST_kKeyPause;
        break;
      case VK_MULTIPLY:
        key = GHOST_kKeyNumpadAsterisk;
        break;
      case VK_SUBTRACT:
        key = GHOST_kKeyNumpadMinus;
        break;
      case VK_DIVIDE:
        key = GHOST_kKeyNumpadSlash;
        break;
      case VK_ADD:
        key = GHOST_kKeyNumpadPlus;
        break;

      case VK_SEMICOLON:
        key = GHOST_kKeySemicolon;
        break;
      case VK_EQUALS:
        key = GHOST_kKeyEqual;
        break;
      case VK_COMMA:
        key = GHOST_kKeyComma;
        break;
      case VK_MINUS:
        key = GHOST_kKeyMinus;
        break;
      case VK_PERIOD:
        key = GHOST_kKeyPeriod;
        break;
      case VK_SLASH:
        key = GHOST_kKeySlash;
        break;
      case VK_BACK_QUOTE:
        key = GHOST_kKeyAccentGrave;
        break;
      case VK_OPEN_BRACKET:
        key = GHOST_kKeyLeftBracket;
        break;
      case VK_BACK_SLASH:
        key = GHOST_kKeyBackslash;
        break;
      case VK_CLOSE_BRACKET:
        key = GHOST_kKeyRightBracket;
        break;
      case VK_QUOTE:
        key = GHOST_kKeyQuote;
        break;
      case VK_GR_LESS:
        key = GHOST_kKeyGrLess;
        break;

      case VK_SHIFT:
        /* Check single shift presses */
        if (scanCode == 0x36) {
          key = GHOST_kKeyRightShift;
        }
        else if (scanCode == 0x2a) {
          key = GHOST_kKeyLeftShift;
        }
        else {
          /* Must be a combination SHIFT (Left or Right) + a Key
           * Ignore this as the next message will contain
           * the desired "Key" */
          key = GHOST_kKeyUnknown;
        }
        break;
      case VK_CONTROL:
        key = (extend) ? GHOST_kKeyRightControl : GHOST_kKeyLeftControl;
        break;
      case VK_MENU:
        key = (extend) ? GHOST_kKeyRightAlt : GHOST_kKeyLeftAlt;
        break;
      case VK_LWIN:
      case VK_RWIN:
        key = GHOST_kKeyOS;
        break;
      case VK_APPS:
        key = GHOST_kKeyApp;
        break;
      case VK_NUMLOCK:
        key = GHOST_kKeyNumLock;
        break;
      case VK_SCROLL:
        key = GHOST_kKeyScrollLock;
        break;
      case VK_CAPITAL:
        key = GHOST_kKeyCapsLock;
        break;
      case VK_OEM_8:
        key = ((GHOST_SystemWin32 *)getSystem())->processSpecialKey(vKey, scanCode);
        break;
      case VK_MEDIA_PLAY_PAUSE:
        key = GHOST_kKeyMediaPlay;
        break;
      case VK_MEDIA_STOP:
        key = GHOST_kKeyMediaStop;
        break;
      case VK_MEDIA_PREV_TRACK:
        key = GHOST_kKeyMediaFirst;
        break;
      case VK_MEDIA_NEXT_TRACK:
        key = GHOST_kKeyMediaLast;
        break;
      default:
        key = GHOST_kKeyUnknown;
        break;
    }
  }

  return key;
}

GHOST_EventButton *GHOST_SystemWin32::processButtonEvent(GHOST_TEventType type,
                                                         GHOST_WindowWin32 *window,
                                                         GHOST_TButtonMask mask)
{
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();

  GHOST_TabletData td = window->getTabletData();

  /* Move mouse to button event position. */
  if (window->getTabletData().Active != GHOST_kTabletModeNone) {
    /* Tablet should be handling in between mouse moves, only move to event position. */
    DWORD msgPos = ::GetMessagePos();
    int msgPosX = GET_X_LPARAM(msgPos);
    int msgPosY = GET_Y_LPARAM(msgPos);
    system->pushEvent(new GHOST_EventCursor(
        ::GetMessageTime(), GHOST_kEventCursorMove, window, msgPosX, msgPosY, td));

    if (type == GHOST_kEventButtonDown) {
      WINTAB_PRINTF("%p OS button down\n", window->getHWND());
    }
    else if (type == GHOST_kEventButtonUp) {
      WINTAB_PRINTF("%p OS button up\n", window->getHWND());
    }
  }

  window->updateMouseCapture(type == GHOST_kEventButtonDown ? MousePressed : MouseReleased);
  return new GHOST_EventButton(system->getMilliSeconds(), type, window, mask, td);
}

void GHOST_SystemWin32::processWintabEvent(GHOST_WindowWin32 *window)
{
  GHOST_Wintab *wt = window->getWintab();
  if (!wt) {
    return;
  }

  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();

  std::vector<GHOST_WintabInfoWin32> wintabInfo;
  wt->getInput(wintabInfo);

  /* Wintab provided coordinates are untrusted until a Wintab and Win32 button down event match.
   * This is checked on every button down event, and revoked if there is a mismatch. This can
   * happen when Wintab incorrectly scales cursor position or is in mouse mode.
   *
   * If Wintab was never trusted while processing this Win32 event, a fallback Ghost cursor move
   * event is created at the position of the Win32 WT_PACKET event. */
  bool mouseMoveHandled;
  bool useWintabPos;
  mouseMoveHandled = useWintabPos = wt->trustCoordinates();

  for (GHOST_WintabInfoWin32 &info : wintabInfo) {
    switch (info.type) {
      case GHOST_kEventCursorMove: {
        if (!useWintabPos) {
          continue;
        }

        wt->mapWintabToSysCoordinates(info.x, info.y, info.x, info.y);
        system->pushEvent(new GHOST_EventCursor(
            info.time, GHOST_kEventCursorMove, window, info.x, info.y, info.tabletData));

        break;
      }
      case GHOST_kEventButtonDown: {
        WINTAB_PRINTF("%p wintab button down", window->getHWND());

        UINT message;
        switch (info.button) {
          case GHOST_kButtonMaskLeft:
            message = WM_LBUTTONDOWN;
            break;
          case GHOST_kButtonMaskRight:
            message = WM_RBUTTONDOWN;
            break;
          case GHOST_kButtonMaskMiddle:
            message = WM_MBUTTONDOWN;
            break;
          default:
            continue;
        }

        /* Wintab buttons are modal, but the API does not inform us what mode a pressed button is
         * in. Only issue button events if we can steal an equivalent Win32 button event from the
         * event queue. */
        MSG msg;
        if (PeekMessage(&msg, window->getHWND(), message, message, PM_NOYIELD) &&
            msg.message != WM_QUIT) {

          /* Test for Win32/Wintab button down match. */
          useWintabPos = wt->testCoordinates(msg.pt.x, msg.pt.y, info.x, info.y);
          if (!useWintabPos) {
            continue;
          }
          else {
            WINTAB_PRINTF(" ... but associated to system button mismatched position\n");
          }

          WINTAB_PRINTF(" ... associated to system button\n");

          /* Steal the Win32 event which was previously peeked. */
          PeekMessage(&msg, window->getHWND(), message, message, PM_REMOVE | PM_NOYIELD);

          /* Move cursor to button location, to prevent incorrect cursor position when
           * transitioning from unsynchronized Win32 to Wintab cursor control. */
          wt->mapWintabToSysCoordinates(info.x, info.y, info.x, info.y);
          system->pushEvent(new GHOST_EventCursor(
              info.time, GHOST_kEventCursorMove, window, info.x, info.y, info.tabletData));

          window->updateMouseCapture(MousePressed);
          system->pushEvent(
              new GHOST_EventButton(info.time, info.type, window, info.button, info.tabletData));

          mouseMoveHandled = true;
          break;
        }
        else {
          WINTAB_PRINTF(" ... but no system button\n");
        }
      }
      case GHOST_kEventButtonUp: {
        WINTAB_PRINTF("%p wintab button up", window->getHWND());
        if (!useWintabPos) {
          WINTAB_PRINTF(" ... but Wintab position isn't trusted\n");
          continue;
        }

        UINT message;
        switch (info.button) {
          case GHOST_kButtonMaskLeft:
            message = WM_LBUTTONUP;
            break;
          case GHOST_kButtonMaskRight:
            message = WM_RBUTTONUP;
            break;
          case GHOST_kButtonMaskMiddle:
            message = WM_MBUTTONUP;
            break;
          default:
            continue;
        }

        /* Wintab buttons are modal, but the API does not inform us what mode a pressed button is
         * in. Only issue button events if we can steal an equivalent Win32 button event from the
         * event queue. */
        MSG msg;
        if (PeekMessage(&msg, window->getHWND(), message, message, PM_REMOVE | PM_NOYIELD) &&
            msg.message != WM_QUIT) {

          WINTAB_PRINTF(" ... associated to system button\n");
          window->updateMouseCapture(MouseReleased);
          system->pushEvent(
              new GHOST_EventButton(info.time, info.type, window, info.button, info.tabletData));
        }
        else {
          WINTAB_PRINTF(" ... but no system button\n");
        }
        break;
      }
      default:
        break;
    }
  }

  /* Fallback cursor movement if Wintab position were never trusted while processing this event. */
  if (!mouseMoveHandled) {
    DWORD pos = GetMessagePos();
    int x = GET_X_LPARAM(pos);
    int y = GET_Y_LPARAM(pos);

    /* TODO supply tablet data */
    system->pushEvent(new GHOST_EventCursor(
        system->getMilliSeconds(), GHOST_kEventCursorMove, window, x, y, GHOST_TABLET_DATA_NONE));
  }
}

void GHOST_SystemWin32::processPointerEvent(
    UINT type, GHOST_WindowWin32 *window, WPARAM wParam, LPARAM lParam, bool &eventHandled)
{
  /* Pointer events might fire when changing windows for a device which is set to use Wintab, even
   * when when Wintab is left enabled but set to the bottom of Wintab overlap order. */
  if (!window->usingTabletAPI(GHOST_kTabletWinPointer)) {
    return;
  }

  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();
  std::vector<GHOST_PointerInfoWin32> pointerInfo;

  if (window->getPointerInfo(pointerInfo, wParam, lParam) != GHOST_kSuccess) {
    return;
  }

  switch (type) {
    case WM_POINTERUPDATE:
      /* Coalesced pointer events are reverse chronological order, reorder chronologically.
       * Only contiguous move events are coalesced. */
      for (GHOST_TUns32 i = pointerInfo.size(); i-- > 0;) {
        system->pushEvent(new GHOST_EventCursor(pointerInfo[i].time,
                                                GHOST_kEventCursorMove,
                                                window,
                                                pointerInfo[i].pixelLocation.x,
                                                pointerInfo[i].pixelLocation.y,
                                                pointerInfo[i].tabletData));
      }

      /* Leave event unhandled so that system cursor is moved. */

      break;
    case WM_POINTERDOWN:
      /* Move cursor to point of contact because GHOST_EventButton does not include position. */
      system->pushEvent(new GHOST_EventCursor(pointerInfo[0].time,
                                              GHOST_kEventCursorMove,
                                              window,
                                              pointerInfo[0].pixelLocation.x,
                                              pointerInfo[0].pixelLocation.y,
                                              pointerInfo[0].tabletData));
      system->pushEvent(new GHOST_EventButton(pointerInfo[0].time,
                                              GHOST_kEventButtonDown,
                                              window,
                                              pointerInfo[0].buttonMask,
                                              pointerInfo[0].tabletData));
      window->updateMouseCapture(MousePressed);

      /* Mark event handled so that mouse button events are not generated. */
      eventHandled = true;

      break;
    case WM_POINTERUP:
      system->pushEvent(new GHOST_EventButton(pointerInfo[0].time,
                                              GHOST_kEventButtonUp,
                                              window,
                                              pointerInfo[0].buttonMask,
                                              pointerInfo[0].tabletData));
      window->updateMouseCapture(MouseReleased);

      /* Mark event handled so that mouse button events are not generated. */
      eventHandled = true;

      break;
    default:
      break;
  }
}

GHOST_EventCursor *GHOST_SystemWin32::processCursorEvent(GHOST_WindowWin32 *window)
{
  GHOST_TInt32 x_screen, y_screen;
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();

  if (window->getTabletData().Active != GHOST_kTabletModeNone) {
    /* While pen devices are in range, cursor movement is handled by tablet input processing. */
    return NULL;
  }

  system->getCursorPosition(x_screen, y_screen);

  if (window->getCursorGrabModeIsWarp()) {
    GHOST_TInt32 x_new = x_screen;
    GHOST_TInt32 y_new = y_screen;
    GHOST_TInt32 x_accum, y_accum;
    GHOST_Rect bounds;

    /* Fallback to window bounds. */
    if (window->getCursorGrabBounds(bounds) == GHOST_kFailure) {
      window->getClientBounds(bounds);
    }

    /* Could also clamp to screen bounds wrap with a window outside the view will fail atm.
     * Use inset in case the window is at screen bounds. */
    bounds.wrapPoint(x_new, y_new, 2, window->getCursorGrabAxis());

    window->getCursorGrabAccum(x_accum, y_accum);
    if (x_new != x_screen || y_new != y_screen) {
      /* When wrapping we don't need to add an event because the setCursorPosition call will cause
       * a new event after. */
      system->setCursorPosition(x_new, y_new); /* wrap */
      window->setCursorGrabAccum(x_accum + (x_screen - x_new), y_accum + (y_screen - y_new));
    }
    else {
      return new GHOST_EventCursor(system->getMilliSeconds(),
                                   GHOST_kEventCursorMove,
                                   window,
                                   x_screen + x_accum,
                                   y_screen + y_accum,
                                   GHOST_TABLET_DATA_NONE);
    }
  }
  else {
    return new GHOST_EventCursor(system->getMilliSeconds(),
                                 GHOST_kEventCursorMove,
                                 window,
                                 x_screen,
                                 y_screen,
                                 GHOST_TABLET_DATA_NONE);
  }
  return NULL;
}

void GHOST_SystemWin32::processWheelEvent(GHOST_WindowWin32 *window, WPARAM wParam, LPARAM lParam)
{
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();

  int acc = system->m_wheelDeltaAccum;
  int delta = GET_WHEEL_DELTA_WPARAM(wParam);

  if (acc * delta < 0) {
    // scroll direction reversed.
    acc = 0;
  }
  acc += delta;
  int direction = (acc >= 0) ? 1 : -1;
  acc = abs(acc);

  while (acc >= WHEEL_DELTA) {
    system->pushEvent(new GHOST_EventWheel(system->getMilliSeconds(), window, direction));
    acc -= WHEEL_DELTA;
  }
  system->m_wheelDeltaAccum = acc * direction;
}

GHOST_EventKey *GHOST_SystemWin32::processKeyEvent(GHOST_WindowWin32 *window, RAWINPUT const &raw)
{
  bool keyDown = false;
  bool is_repeated_modifier = false;
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();
  GHOST_TKey key = system->hardKey(raw, &keyDown, &is_repeated_modifier);
  GHOST_EventKey *event;

  /* We used to check `if (key != GHOST_kKeyUnknown)`, but since the message
   * values `WM_SYSKEYUP`, `WM_KEYUP` and `WM_CHAR` are ignored, we capture
   * those events here as well. */
  if (!is_repeated_modifier) {
    char vk = raw.data.keyboard.VKey;
    char utf8_char[6] = {0};
    char ascii = 0;
    bool is_repeat = false;

    /* Unlike on Linux, not all keys can send repeat events. E.g. modifier keys don't. */
    if (keyDown) {
      if (system->m_keycode_last_repeat_key == vk) {
        is_repeat = true;
      }
      system->m_keycode_last_repeat_key = vk;
    }
    else {
      if (system->m_keycode_last_repeat_key == vk) {
        system->m_keycode_last_repeat_key = 0;
      }
    }

    wchar_t utf16[3] = {0};
    BYTE state[256] = {0};
    int r;
    GetKeyboardState((PBYTE)state);
    bool ctrl_pressed = state[VK_CONTROL] & 0x80;
    bool alt_pressed = state[VK_MENU] & 0x80;

    /* No text with control key pressed (Alt can be used to insert special characters though!). */
    if (ctrl_pressed && !alt_pressed) {
      utf8_char[0] = '\0';
    }
    // Don't call ToUnicodeEx on dead keys as it clears the buffer and so won't allow diacritical
    // composition.
    else if (MapVirtualKeyW(vk, 2) != 0) {
      // todo: ToUnicodeEx can respond with up to 4 utf16 chars (only 2 here).
      // Could be up to 24 utf8 bytes.
      if ((r = ToUnicodeEx(
               vk, raw.data.keyboard.MakeCode, state, utf16, 2, 0, system->m_keylayout))) {
        if ((r > 0 && r < 3)) {
          utf16[r] = 0;
          conv_utf_16_to_8(utf16, utf8_char, 6);
        }
        else if (r == -1) {
          utf8_char[0] = '\0';
        }
      }
    }

    if (!keyDown) {
      utf8_char[0] = '\0';
      ascii = '\0';
    }
    else {
      ascii = utf8_char[0] & 0x80 ? '?' : utf8_char[0];
    }

    event = new GHOST_EventKey(system->getMilliSeconds(),
                               keyDown ? GHOST_kEventKeyDown : GHOST_kEventKeyUp,
                               window,
                               key,
                               ascii,
                               utf8_char,
                               is_repeat);

    // GHOST_PRINTF("%c\n", ascii); // we already get this info via EventPrinter
  }
  else {
    event = NULL;
  }

  return event;
}

GHOST_Event *GHOST_SystemWin32::processWindowSizeEvent(GHOST_WindowWin32 *window)
{
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();
  GHOST_Event *sizeEvent = new GHOST_Event(
      system->getMilliSeconds(), GHOST_kEventWindowSize, window);

  /* We get WM_SIZE before we fully init. Do not dispatch before we are continuously resizing. */
  if (window->m_inLiveResize) {
    system->pushEvent(sizeEvent);
    system->dispatchEvents();
    return NULL;
  }
  else {
    return sizeEvent;
  }
}

GHOST_Event *GHOST_SystemWin32::processWindowEvent(GHOST_TEventType type,
                                                   GHOST_WindowWin32 *window)
{
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();

  if (type == GHOST_kEventWindowActivate) {
    system->getWindowManager()->setActiveWindow(window);
  }

  return new GHOST_Event(system->getMilliSeconds(), type, window);
}

#ifdef WITH_INPUT_IME
GHOST_Event *GHOST_SystemWin32::processImeEvent(GHOST_TEventType type,
                                                GHOST_WindowWin32 *window,
                                                GHOST_TEventImeData *data)
{
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();
  return new GHOST_EventIME(system->getMilliSeconds(), type, window, data);
}
#endif

GHOST_TSuccess GHOST_SystemWin32::pushDragDropEvent(GHOST_TEventType eventType,
                                                    GHOST_TDragnDropTypes draggedObjectType,
                                                    GHOST_WindowWin32 *window,
                                                    int mouseX,
                                                    int mouseY,
                                                    void *data)
{
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();
  return system->pushEvent(new GHOST_EventDragnDrop(
      system->getMilliSeconds(), eventType, draggedObjectType, window, mouseX, mouseY, data));
}

void GHOST_SystemWin32::setTabletAPI(GHOST_TTabletAPI api)
{
  GHOST_System::setTabletAPI(api);

  /* If API is set to WinPointer (Windows Ink), unload Wintab so that trouble drivers don't disable
   * Windows Ink. Load Wintab when API is Automatic because decision logic relies on knowing
   * whether a Wintab device is present. */
  const bool loadWintab = GHOST_kTabletWinPointer != api;
  GHOST_WindowManager *wm = getWindowManager();

  for (GHOST_IWindow *win : wm->getWindows()) {
    GHOST_WindowWin32 *windowWin32 = (GHOST_WindowWin32 *)win;
    if (loadWintab) {
      windowWin32->loadWintab(GHOST_kWindowStateMinimized != windowWin32->getState());

      if (windowWin32->usingTabletAPI(GHOST_kTabletWintab)) {
        windowWin32->resetPointerPenInfo();
      }
    }
    else {
      windowWin32->closeWintab();
    }
  }
}

void GHOST_SystemWin32::processMinMaxInfo(MINMAXINFO *minmax)
{
  minmax->ptMinTrackSize.x = 320;
  minmax->ptMinTrackSize.y = 240;
}

#ifdef WITH_INPUT_NDOF
bool GHOST_SystemWin32::processNDOF(RAWINPUT const &raw)
{
  bool eventSent = false;
  GHOST_TUns64 now = getMilliSeconds();

  static bool firstEvent = true;
  if (firstEvent) {  // determine exactly which device is plugged in
    RID_DEVICE_INFO info;
    unsigned infoSize = sizeof(RID_DEVICE_INFO);
    info.cbSize = infoSize;

    GetRawInputDeviceInfo(raw.header.hDevice, RIDI_DEVICEINFO, &info, &infoSize);
    if (info.dwType == RIM_TYPEHID)
      m_ndofManager->setDevice(info.hid.dwVendorId, info.hid.dwProductId);
    else
      GHOST_PRINT("<!> not a HID device... mouse/kb perhaps?\n");

    firstEvent = false;
  }

  // The NDOF manager sends button changes immediately, and *pretends* to
  // send motion. Mark as 'sent' so motion will always get dispatched.
  eventSent = true;

  BYTE const *data = raw.data.hid.bRawData;

  BYTE packetType = data[0];
  switch (packetType) {
    case 1:  // translation
    {
      const short *axis = (short *)(data + 1);
      // massage into blender view coords (same goes for rotation)
      const int t[3] = {axis[0], -axis[2], axis[1]};
      m_ndofManager->updateTranslation(t, now);

      if (raw.data.hid.dwSizeHid == 13) {
        // this report also includes rotation
        const int r[3] = {-axis[3], axis[5], -axis[4]};
        m_ndofManager->updateRotation(r, now);

        // I've never gotten one of these, has anyone else?
        GHOST_PRINT("ndof: combined T + R\n");
      }
      break;
    }
    case 2:  // rotation
    {
      const short *axis = (short *)(data + 1);
      const int r[3] = {-axis[0], axis[2], -axis[1]};
      m_ndofManager->updateRotation(r, now);
      break;
    }
    case 3:  // buttons
    {
      int button_bits;
      memcpy(&button_bits, data + 1, sizeof(button_bits));
      m_ndofManager->updateButtons(button_bits, now);
      break;
    }
  }
  return eventSent;
}
#endif  // WITH_INPUT_NDOF

LRESULT WINAPI GHOST_SystemWin32::s_wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  GHOST_Event *event = NULL;
  bool eventHandled = false;

  LRESULT lResult = 0;
  GHOST_SystemWin32 *system = (GHOST_SystemWin32 *)getSystem();
#ifdef WITH_INPUT_IME
  GHOST_EventManager *eventManager = system->getEventManager();
#endif
  GHOST_ASSERT(system, "GHOST_SystemWin32::s_wndProc(): system not initialized");

  if (hwnd) {

    if (msg == WM_NCCREATE) {
      // Tell Windows to automatically handle scaling of non-client areas
      // such as the caption bar. EnableNonClientDpiScaling was introduced in Windows 10
      HMODULE m_user32 = ::LoadLibrary("User32.dll");
      if (m_user32) {
        GHOST_WIN32_EnableNonClientDpiScaling fpEnableNonClientDpiScaling =
            (GHOST_WIN32_EnableNonClientDpiScaling)::GetProcAddress(m_user32,
                                                                    "EnableNonClientDpiScaling");
        if (fpEnableNonClientDpiScaling) {
          fpEnableNonClientDpiScaling(hwnd);
        }
      }
    }

    GHOST_WindowWin32 *window = (GHOST_WindowWin32 *)::GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (window) {
      switch (msg) {
        // we need to check if new key layout has AltGr
        case WM_INPUTLANGCHANGE: {
          system->handleKeyboardChange();
#ifdef WITH_INPUT_IME
          window->getImeInput()->SetInputLanguage();
#endif
          break;
        }
        ////////////////////////////////////////////////////////////////////////
        // Keyboard events, processed
        ////////////////////////////////////////////////////////////////////////
        case WM_INPUT: {
          RAWINPUT raw;
          RAWINPUT *raw_ptr = &raw;
          UINT rawSize = sizeof(RAWINPUT);

          GetRawInputData((HRAWINPUT)lParam, RID_INPUT, raw_ptr, &rawSize, sizeof(RAWINPUTHEADER));

          switch (raw.header.dwType) {
            case RIM_TYPEKEYBOARD:
              event = processKeyEvent(window, raw);
              if (!event) {
                GHOST_PRINT("GHOST_SystemWin32::wndProc: key event ");
                GHOST_PRINT(msg);
                GHOST_PRINT(" key ignored\n");
              }
              break;
#ifdef WITH_INPUT_NDOF
            case RIM_TYPEHID:
              if (system->processNDOF(raw)) {
                eventHandled = true;
              }
              break;
#endif
          }
          break;
        }
#ifdef WITH_INPUT_IME
        ////////////////////////////////////////////////////////////////////////
        // IME events, processed, read more in GHOST_IME.h
        ////////////////////////////////////////////////////////////////////////
        case WM_IME_SETCONTEXT: {
          GHOST_ImeWin32 *ime = window->getImeInput();
          ime->SetInputLanguage();
          ime->CreateImeWindow(hwnd);
          ime->CleanupComposition(hwnd);
          ime->CheckFirst(hwnd);
          break;
        }
        case WM_IME_STARTCOMPOSITION: {
          GHOST_ImeWin32 *ime = window->getImeInput();
          eventHandled = true;
          /* remove input event before start comp event, avoid redundant input */
          eventManager->removeTypeEvents(GHOST_kEventKeyDown, window);
          ime->CreateImeWindow(hwnd);
          ime->ResetComposition(hwnd);
          event = processImeEvent(GHOST_kEventImeCompositionStart, window, &ime->eventImeData);
          break;
        }
        case WM_IME_COMPOSITION: {
          GHOST_ImeWin32 *ime = window->getImeInput();
          eventHandled = true;
          ime->UpdateImeWindow(hwnd);
          ime->UpdateInfo(hwnd);
          if (ime->eventImeData.result_len) {
            /* remove redundant IME event */
            eventManager->removeTypeEvents(GHOST_kEventImeComposition, window);
          }
          event = processImeEvent(GHOST_kEventImeComposition, window, &ime->eventImeData);
          break;
        }
        case WM_IME_ENDCOMPOSITION: {
          GHOST_ImeWin32 *ime = window->getImeInput();
          eventHandled = true;
          /* remove input event after end comp event, avoid redundant input */
          eventManager->removeTypeEvents(GHOST_kEventKeyDown, window);
          ime->ResetComposition(hwnd);
          ime->DestroyImeWindow(hwnd);
          event = processImeEvent(GHOST_kEventImeCompositionEnd, window, &ime->eventImeData);
          break;
        }
#endif /* WITH_INPUT_IME */
        ////////////////////////////////////////////////////////////////////////
        // Keyboard events, ignored
        ////////////////////////////////////////////////////////////////////////
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        /* These functions were replaced by WM_INPUT*/
        case WM_CHAR:
        /* The WM_CHAR message is posted to the window with the keyboard focus when
         * a WM_KEYDOWN message is translated by the TranslateMessage function. WM_CHAR
         * contains the character code of the key that was pressed.
         */
        case WM_DEADCHAR:
          /* The WM_DEADCHAR message is posted to the window with the keyboard focus when a
           * WM_KEYUP message is translated by the TranslateMessage function. WM_DEADCHAR
           * specifies a character code generated by a dead key. A dead key is a key that
           * generates a character, such as the umlaut (double-dot), that is combined with
           * another character to form a composite character. For example, the umlaut-O
           * character (Ö) is generated by typing the dead key for the umlaut character, and
           * then typing the O key.
           */
          break;
        case WM_SYSDEADCHAR:
        /* The WM_SYSDEADCHAR message is sent to the window with the keyboard focus when
         * a WM_SYSKEYDOWN message is translated by the TranslateMessage function.
         * WM_SYSDEADCHAR specifies the character code of a system dead key - that is,
         * a dead key that is pressed while holding down the alt key.
         */
        case WM_SYSCHAR:
          /* The WM_SYSCHAR message is sent to the window with the keyboard focus when
           * a WM_SYSCHAR message is translated by the TranslateMessage function.
           * WM_SYSCHAR specifies the character code of a dead key - that is,
           * a dead key that is pressed while holding down the alt key.
           * To prevent the sound, DefWindowProc must be avoided by return
           */
          break;
        case WM_SYSCOMMAND:
          /* The WM_SYSCOMMAND message is sent to the window when system commands such as
           * maximize, minimize  or close the window are triggered. Also it is sent when ALT
           * button is press for menu. To prevent this we must return preventing DefWindowProc.
           *
           * Note that the four low-order bits of the wParam parameter are used internally by the
           * OS. To obtain the correct result when testing the value of wParam, an application
           * must combine the value 0xFFF0 with the wParam value by using the bitwise AND operator.
           */
          switch (wParam & 0xFFF0) {
            case SC_KEYMENU:
              eventHandled = true;
              break;
            case SC_RESTORE: {
              ::ShowWindow(hwnd, SW_RESTORE);
              window->setState(window->getState());

              GHOST_Wintab *wt = window->getWintab();
              if (wt) {
                wt->enable();
              }

              eventHandled = true;
              break;
            }
            case SC_MAXIMIZE: {
              GHOST_Wintab *wt = window->getWintab();
              if (wt) {
                wt->enable();
              }
              /* Don't report event as handled so that default handling occurs. */
              break;
            }
            case SC_MINIMIZE: {
              GHOST_Wintab *wt = window->getWintab();
              if (wt) {
                wt->disable();
              }
              /* Don't report event as handled so that default handling occurs. */
              break;
            }
          }
          break;
        ////////////////////////////////////////////////////////////////////////
        // Wintab events, processed
        ////////////////////////////////////////////////////////////////////////
        case WT_CSRCHANGE: {
          WINTAB_PRINTF("%p WT_CSRCHANGE\n", window->getHWND());
          GHOST_Wintab *wt = window->getWintab();
          if (wt) {
            wt->updateCursorInfo();
          }
          eventHandled = true;
          break;
        }
        case WT_PROXIMITY: {
          WINTAB_PRINTF(
              "%p WT_PROXIMITY loword (!0 enter 0 leave context): %d, hiword (!0 enter !0 leave "
              "hardware): %d\n",
              window->getHWND(),
              LOWORD(lParam),
              HIWORD(lParam));
          GHOST_Wintab *wt = window->getWintab();
          if (wt) {
            bool inRange = LOWORD(lParam);
            if (inRange) {
              /* Some devices don't emit WT_CSRCHANGE events, so update cursor info here. */
              wt->updateCursorInfo();
            }
            else {
              wt->leaveRange();
            }
          }
          eventHandled = true;
          break;
        }
        case WT_INFOCHANGE: {
          WINTAB_PRINTF("%p WT_INFOCHANGE\n", window->getHWND());
          GHOST_Wintab *wt = window->getWintab();
          if (wt) {
            wt->processInfoChange(lParam);

            if (window->usingTabletAPI(GHOST_kTabletWintab)) {
              window->resetPointerPenInfo();
            }
          }
          eventHandled = true;
          break;
        }
        case WT_PACKET:
          processWintabEvent(window);
          eventHandled = true;
          break;
        ////////////////////////////////////////////////////////////////////////
        // Wintab events, debug
        ////////////////////////////////////////////////////////////////////////
        case WT_CTXOPEN:
          WINTAB_PRINTF("%p WT_CTXOPEN\n", window->getHWND());
          break;
        case WT_CTXCLOSE:
          WINTAB_PRINTF("%p WT_CTXCLOSE\n", window->getHWND());
          break;
        case WT_CTXUPDATE:
          WINTAB_PRINTF("%p WT_CTXUPDATE\n", window->getHWND());
          break;
        case WT_CTXOVERLAP:
          switch (lParam) {
            case CXS_DISABLED:
              WINTAB_PRINTF("%p WT_CTXOVERLAP CXS_DISABLED\n", window->getHWND());
              break;
            case CXS_OBSCURED:
              WINTAB_PRINTF("%p WT_CTXOVERLAP CXS_OBSCURED\n", window->getHWND());
              break;
            case CXS_ONTOP:
              WINTAB_PRINTF("%p WT_CTXOVERLAP CXS_ONTOP\n", window->getHWND());
              break;
          }
          break;
        ////////////////////////////////////////////////////////////////////////
        // Pointer events, processed
        ////////////////////////////////////////////////////////////////////////
        case WM_POINTERUPDATE:
        case WM_POINTERDOWN:
        case WM_POINTERUP:
          processPointerEvent(msg, window, wParam, lParam, eventHandled);
          break;
        case WM_POINTERLEAVE: {
          GHOST_TUns32 pointerId = GET_POINTERID_WPARAM(wParam);
          POINTER_INFO pointerInfo;
          if (!GetPointerInfo(pointerId, &pointerInfo)) {
            break;
          }

          /* Reset pointer pen info if pen device has left tracking range. */
          if (pointerInfo.pointerType == PT_PEN && !IS_POINTER_INRANGE_WPARAM(wParam)) {
            window->resetPointerPenInfo();
            eventHandled = true;
          }
          break;
        }
        ////////////////////////////////////////////////////////////////////////
        // Mouse events, processed
        ////////////////////////////////////////////////////////////////////////
        case WM_LBUTTONDOWN:
          event = processButtonEvent(GHOST_kEventButtonDown, window, GHOST_kButtonMaskLeft);
          break;
        case WM_MBUTTONDOWN:
          event = processButtonEvent(GHOST_kEventButtonDown, window, GHOST_kButtonMaskMiddle);
          break;
        case WM_RBUTTONDOWN:
          event = processButtonEvent(GHOST_kEventButtonDown, window, GHOST_kButtonMaskRight);
          break;
        case WM_XBUTTONDOWN:
          if ((short)HIWORD(wParam) == XBUTTON1) {
            event = processButtonEvent(GHOST_kEventButtonDown, window, GHOST_kButtonMaskButton4);
          }
          else if ((short)HIWORD(wParam) == XBUTTON2) {
            event = processButtonEvent(GHOST_kEventButtonDown, window, GHOST_kButtonMaskButton5);
          }
          break;
        case WM_LBUTTONUP:
          event = processButtonEvent(GHOST_kEventButtonUp, window, GHOST_kButtonMaskLeft);
          break;
        case WM_MBUTTONUP:
          event = processButtonEvent(GHOST_kEventButtonUp, window, GHOST_kButtonMaskMiddle);
          break;
        case WM_RBUTTONUP:
          event = processButtonEvent(GHOST_kEventButtonUp, window, GHOST_kButtonMaskRight);
          break;
        case WM_XBUTTONUP:
          if ((short)HIWORD(wParam) == XBUTTON1) {
            event = processButtonEvent(GHOST_kEventButtonUp, window, GHOST_kButtonMaskButton4);
          }
          else if ((short)HIWORD(wParam) == XBUTTON2) {
            event = processButtonEvent(GHOST_kEventButtonUp, window, GHOST_kButtonMaskButton5);
          }
          break;
        case WM_MOUSEMOVE:
          if (!window->m_mousePresent) {
            WINTAB_PRINTF("%p mouse enter\n", window->getHWND());
            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            window->m_mousePresent = true;
            GHOST_Wintab *wt = window->getWintab();
            if (wt) {
              wt->gainFocus();
            }
          }

          event = processCursorEvent(window);

          break;
        case WM_MOUSEWHEEL: {
          /* The WM_MOUSEWHEEL message is sent to the focus window
           * when the mouse wheel is rotated. The DefWindowProc
           * function propagates the message to the window's parent.
           * There should be no internal forwarding of the message,
           * since DefWindowProc propagates it up the parent chain
           * until it finds a window that processes it.
           */
          processWheelEvent(window, wParam, lParam);
          eventHandled = true;
#ifdef BROKEN_PEEK_TOUCHPAD
          PostMessage(hwnd, WM_USER, 0, 0);
#endif
          break;
        }
        case WM_SETCURSOR:
          /* The WM_SETCURSOR message is sent to a window if the mouse causes the cursor
           * to move within a window and mouse input is not captured.
           * This means we have to set the cursor shape every time the mouse moves!
           * The DefWindowProc function uses this message to set the cursor to an
           * arrow if it is not in the client area.
           */
          if (LOWORD(lParam) == HTCLIENT) {
            // Load the current cursor
            window->loadCursor(window->getCursorVisibility(), window->getCursorShape());
            // Bypass call to DefWindowProc
            return 0;
          }
          else {
            // Outside of client area show standard cursor
            window->loadCursor(true, GHOST_kStandardCursorDefault);
          }
          break;
        case WM_MOUSELEAVE: {
          WINTAB_PRINTF("%p mouse leave\n", window->getHWND());
          window->m_mousePresent = false;
          if (window->getTabletData().Active == GHOST_kTabletModeNone) {
            processCursorEvent(window);
          }
          GHOST_Wintab *wt = window->getWintab();
          if (wt) {
            wt->loseFocus();
          }
          break;
        }
        ////////////////////////////////////////////////////////////////////////
        // Mouse events, ignored
        ////////////////////////////////////////////////////////////////////////
        case WM_NCMOUSEMOVE:
        /* The WM_NCMOUSEMOVE message is posted to a window when the cursor is moved
         * within the non-client area of the window. This message is posted to the window that
         * contains the cursor. If a window has captured the mouse, this message is not posted.
         */
        case WM_NCHITTEST:
          /* The WM_NCHITTEST message is sent to a window when the cursor moves, or
           * when a mouse button is pressed or released. If the mouse is not captured,
           * the message is sent to the window beneath the cursor. Otherwise, the message
           * is sent to the window that has captured the mouse.
           */
          break;

        ////////////////////////////////////////////////////////////////////////
        // Window events, processed
        ////////////////////////////////////////////////////////////////////////
        case WM_CLOSE:
          /* The WM_CLOSE message is sent as a signal that a window
           * or an application should terminate. Restore if minimized. */
          if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
          }
          event = processWindowEvent(GHOST_kEventWindowClose, window);
          break;
        case WM_ACTIVATE:
          /* The WM_ACTIVATE message is sent to both the window being activated and the window
           * being deactivated. If the windows use the same input queue, the message is sent
           * synchronously, first to the window procedure of the top-level window being
           * deactivated, then to the window procedure of the top-level window being activated.
           * If the windows use different input queues, the message is sent asynchronously,
           * so the window is activated immediately. */
          {
            GHOST_ModifierKeys modifiers;
            modifiers.clear();
            system->storeModifierKeys(modifiers);
            system->m_wheelDeltaAccum = 0;
            system->m_keycode_last_repeat_key = 0;
            event = processWindowEvent(LOWORD(wParam) ? GHOST_kEventWindowActivate :
                                                        GHOST_kEventWindowDeactivate,
                                       window);
            /* WARNING: Let DefWindowProc handle WM_ACTIVATE, otherwise WM_MOUSEWHEEL
             * will not be dispatched to OUR active window if we minimize one of OUR windows. */
            if (LOWORD(wParam) == WA_INACTIVE)
              window->lostMouseCapture();

            lResult = ::DefWindowProc(hwnd, msg, wParam, lParam);
            break;
          }
        case WM_ENTERSIZEMOVE:
          /* The WM_ENTERSIZEMOVE message is sent one time to a window after it enters the moving
           * or sizing modal loop. The window enters the moving or sizing modal loop when the user
           * clicks the window's title bar or sizing border, or when the window passes the
           * WM_SYSCOMMAND message to the DefWindowProc function and the wParam parameter of the
           * message specifies the SC_MOVE or SC_SIZE value. The operation is complete when
           * DefWindowProc returns.
           */
          window->m_inLiveResize = 1;
          break;
        case WM_EXITSIZEMOVE:
          window->m_inLiveResize = 0;
          break;
        case WM_PAINT:
          /* An application sends the WM_PAINT message when the system or another application
           * makes a request to paint a portion of an application's window. The message is sent
           * when the UpdateWindow or RedrawWindow function is called, or by the DispatchMessage
           * function when the application obtains a WM_PAINT message by using the GetMessage or
           * PeekMessage function.
           */
          if (!window->m_inLiveResize) {
            event = processWindowEvent(GHOST_kEventWindowUpdate, window);
            ::ValidateRect(hwnd, NULL);
          }
          else {
            eventHandled = true;
          }
          break;
        case WM_GETMINMAXINFO:
          /* The WM_GETMINMAXINFO message is sent to a window when the size or
           * position of the window is about to change. An application can use
           * this message to override the window's default maximized size and
           * position, or its default minimum or maximum tracking size.
           */
          processMinMaxInfo((MINMAXINFO *)lParam);
          /* Let DefWindowProc handle it. */
          break;
        case WM_SIZING:
          event = processWindowSizeEvent(window);
          break;
        case WM_SIZE:
          /* The WM_SIZE message is sent to a window after its size has changed.
           * The WM_SIZE and WM_MOVE messages are not sent if an application handles the
           * WM_WINDOWPOSCHANGED message without calling DefWindowProc. It is more efficient
           * to perform any move or size change processing during the WM_WINDOWPOSCHANGED
           * message without calling DefWindowProc.
           */
          event = processWindowSizeEvent(window);
          break;
        case WM_CAPTURECHANGED:
          window->lostMouseCapture();
          break;
        case WM_MOVING:
          /* The WM_MOVING message is sent to a window that the user is moving. By processing
           * this message, an application can monitor the size and position of the drag rectangle
           * and, if needed, change its size or position.
           */
        case WM_MOVE:
          /* The WM_SIZE and WM_MOVE messages are not sent if an application handles the
           * WM_WINDOWPOSCHANGED message without calling DefWindowProc. It is more efficient
           * to perform any move or size change processing during the WM_WINDOWPOSCHANGED
           * message without calling DefWindowProc.
           */
          /* see WM_SIZE comment*/
          if (window->m_inLiveResize) {
            system->pushEvent(processWindowEvent(GHOST_kEventWindowMove, window));
            system->dispatchEvents();
          }
          else {
            event = processWindowEvent(GHOST_kEventWindowMove, window);
          }

          break;
        case WM_DPICHANGED:
          /* The WM_DPICHANGED message is sent when the effective dots per inch (dpi) for a
           * window has changed. The DPI is the scale factor for a window. There are multiple
           * events that can cause the DPI to change such as when the window is moved to a monitor
           * with a different DPI.
           */
          {
            // The suggested new size and position of the window.
            RECT *const suggestedWindowRect = (RECT *)lParam;

            // Push DPI change event first
            system->pushEvent(processWindowEvent(GHOST_kEventWindowDPIHintChanged, window));
            system->dispatchEvents();
            eventHandled = true;

            // Then move and resize window
            SetWindowPos(hwnd,
                         NULL,
                         suggestedWindowRect->left,
                         suggestedWindowRect->top,
                         suggestedWindowRect->right - suggestedWindowRect->left,
                         suggestedWindowRect->bottom - suggestedWindowRect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
          }
          break;
        case WM_DISPLAYCHANGE: {
          GHOST_Wintab *wt = window->getWintab();
          if (wt) {
            for (GHOST_IWindow *iter_win : system->getWindowManager()->getWindows()) {
              GHOST_WindowWin32 *iter_win32win = (GHOST_WindowWin32 *)iter_win;
              wt->remapCoordinates();
            }
          }
          break;
        }
        case WM_KILLFOCUS:
          /* The WM_KILLFOCUS message is sent to a window immediately before it loses the keyboard
           * focus. We want to prevent this if a window is still active and it loses focus to
           * nowhere. */
          if (!wParam && hwnd == ::GetActiveWindow()) {
            ::SetFocus(hwnd);
          }
          break;
        ////////////////////////////////////////////////////////////////////////
        // Window events, ignored
        ////////////////////////////////////////////////////////////////////////
        case WM_WINDOWPOSCHANGED:
        /* The WM_WINDOWPOSCHANGED message is sent to a window whose size, position, or place
         * in the Z order has changed as a result of a call to the SetWindowPos function or
         * another window-management function.
         * The WM_SIZE and WM_MOVE messages are not sent if an application handles the
         * WM_WINDOWPOSCHANGED message without calling DefWindowProc. It is more efficient
         * to perform any move or size change processing during the WM_WINDOWPOSCHANGED
         * message without calling DefWindowProc.
         */
        case WM_ERASEBKGND:
        /* An application sends the WM_ERASEBKGND message when the window background must be
         * erased (for example, when a window is resized). The message is sent to prepare an
         * invalidated portion of a window for painting.
         */
        case WM_NCPAINT:
        /* An application sends the WM_NCPAINT message to a window
         * when its frame must be painted. */
        case WM_NCACTIVATE:
        /* The WM_NCACTIVATE message is sent to a window when its non-client area needs to be
         * changed to indicate an active or inactive state. */
        case WM_DESTROY:
        /* The WM_DESTROY message is sent when a window is being destroyed. It is sent to the
         * window procedure of the window being destroyed after the window is removed from the
         * screen. This message is sent first to the window being destroyed and then to the child
         * windows (if any) as they are destroyed. During the processing of the message, it can
         * be assumed that all child windows still exist. */
        case WM_NCDESTROY:
          /* The WM_NCDESTROY message informs a window that its non-client area is being
           * destroyed. The DestroyWindow function sends the WM_NCDESTROY message to the window
           * following the WM_DESTROY message. WM_DESTROY is used to free the allocated memory
           * object associated with the window.
           */
          break;
        case WM_SHOWWINDOW:
        /* The WM_SHOWWINDOW message is sent to a window when the window is
         * about to be hidden or shown. */
        case WM_WINDOWPOSCHANGING:
        /* The WM_WINDOWPOSCHANGING message is sent to a window whose size, position, or place in
         * the Z order is about to change as a result of a call to the SetWindowPos function or
         * another window-management function.
         */
        case WM_SETFOCUS:
          /* The WM_SETFOCUS message is sent to a window after it has gained the keyboard focus. */
          break;
        ////////////////////////////////////////////////////////////////////////
        // Other events
        ////////////////////////////////////////////////////////////////////////
        case WM_GETTEXT:
        /* An application sends a WM_GETTEXT message to copy the text that
         * corresponds to a window into a buffer provided by the caller.
         */
        case WM_ACTIVATEAPP:
        /* The WM_ACTIVATEAPP message is sent when a window belonging to a
         * different application than the active window is about to be activated.
         * The message is sent to the application whose window is being activated
         * and to the application whose window is being deactivated.
         */
        case WM_TIMER:
          /* The WIN32 docs say:
           * The WM_TIMER message is posted to the installing thread's message queue
           * when a timer expires. You can process the message by providing a WM_TIMER
           * case in the window procedure. Otherwise, the default window procedure will
           * call the TimerProc callback function specified in the call to the SetTimer
           * function used to install the timer.
           *
           * In GHOST, we let DefWindowProc call the timer callback.
           */
          break;
      }
    }
    else {
      // Event found for a window before the pointer to the class has been set.
      GHOST_PRINT("GHOST_SystemWin32::wndProc: GHOST window event before creation\n");
      /* These are events we typically miss at this point:
       * WM_GETMINMAXINFO 0x24
       * WM_NCCREATE          0x81
       * WM_NCCALCSIZE        0x83
       * WM_CREATE            0x01
       * We let DefWindowProc do the work.
       */
    }
  }
  else {
    // Events without valid hwnd
    GHOST_PRINT("GHOST_SystemWin32::wndProc: event without window\n");
  }

  if (event) {
    system->pushEvent(event);
    eventHandled = true;
  }

  if (!eventHandled)
    lResult = ::DefWindowProcW(hwnd, msg, wParam, lParam);

  return lResult;
}

GHOST_TUns8 *GHOST_SystemWin32::getClipboard(bool selection) const
{
  char *temp_buff;

  if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(NULL)) {
    wchar_t *buffer;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL) {
      CloseClipboard();
      return NULL;
    }
    buffer = (wchar_t *)GlobalLock(hData);
    if (!buffer) {
      CloseClipboard();
      return NULL;
    }

    temp_buff = alloc_utf_8_from_16(buffer, 0);

    /* Buffer mustn't be accessed after CloseClipboard
     * it would like accessing free-d memory */
    GlobalUnlock(hData);
    CloseClipboard();

    return (GHOST_TUns8 *)temp_buff;
  }
  else if (IsClipboardFormatAvailable(CF_TEXT) && OpenClipboard(NULL)) {
    char *buffer;
    size_t len = 0;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == NULL) {
      CloseClipboard();
      return NULL;
    }
    buffer = (char *)GlobalLock(hData);
    if (!buffer) {
      CloseClipboard();
      return NULL;
    }

    len = strlen(buffer);
    temp_buff = (char *)malloc(len + 1);
    strncpy(temp_buff, buffer, len);
    temp_buff[len] = '\0';

    /* Buffer mustn't be accessed after CloseClipboard
     * it would like accessing free-d memory */
    GlobalUnlock(hData);
    CloseClipboard();

    return (GHOST_TUns8 *)temp_buff;
  }
  else {
    return NULL;
  }
}

void GHOST_SystemWin32::putClipboard(GHOST_TInt8 *buffer, bool selection) const
{
  if (selection) {
    return;
  }  // for copying the selection, used on X11

  if (OpenClipboard(NULL)) {
    HLOCAL clipbuffer;
    wchar_t *data;

    if (buffer) {
      size_t len = count_utf_16_from_8(buffer);
      EmptyClipboard();

      clipbuffer = LocalAlloc(LMEM_FIXED, sizeof(wchar_t) * len);
      data = (wchar_t *)GlobalLock(clipbuffer);

      conv_utf_8_to_16(buffer, data, len);

      LocalUnlock(clipbuffer);
      SetClipboardData(CF_UNICODETEXT, clipbuffer);
    }
    CloseClipboard();
  }
  else {
    return;
  }
}

/* -------------------------------------------------------------------- */
/** \name Message Box
 * \{ */

GHOST_TSuccess GHOST_SystemWin32::showMessageBox(const char *title,
                                                 const char *message,
                                                 const char *help_label,
                                                 const char *continue_label,
                                                 const char *link,
                                                 GHOST_DialogOptions dialog_options) const
{
  const wchar_t *title_16 = alloc_utf16_from_8(title, 0);
  const wchar_t *message_16 = alloc_utf16_from_8(message, 0);
  const wchar_t *help_label_16 = alloc_utf16_from_8(help_label, 0);
  const wchar_t *continue_label_16 = alloc_utf16_from_8(continue_label, 0);

  int nButtonPressed = 0;
  TASKDIALOGCONFIG config = {0};
  const TASKDIALOG_BUTTON buttons[] = {{IDOK, help_label_16}, {IDCONTINUE, continue_label_16}};

  config.cbSize = sizeof(config);
  config.hInstance = 0;
  config.dwCommonButtons = 0;
  config.pszMainIcon = (dialog_options & GHOST_DialogError ?
                            TD_ERROR_ICON :
                            dialog_options & GHOST_DialogWarning ? TD_WARNING_ICON :
                                                                   TD_INFORMATION_ICON);
  config.pszWindowTitle = L"Blender";
  config.pszMainInstruction = title_16;
  config.pszContent = message_16;
  config.pButtons = (link) ? buttons : buttons + 1;
  config.cButtons = (link) ? 2 : 1;

  TaskDialogIndirect(&config, &nButtonPressed, NULL, NULL);
  switch (nButtonPressed) {
    case IDOK:
      ShellExecute(NULL, "open", link, NULL, NULL, SW_SHOWNORMAL);
      break;
    case IDCONTINUE:
      break;
    default:
      break;  // should never happen
  }

  free((void *)title_16);
  free((void *)message_16);
  free((void *)help_label_16);
  free((void *)continue_label_16);

  return GHOST_kSuccess;
}

/** \} */

static DWORD GetParentProcessID(void)
{
  HANDLE snapshot;
  PROCESSENTRY32 pe32 = {0};
  DWORD ppid = 0, pid = GetCurrentProcessId();
  snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return -1;
  }
  pe32.dwSize = sizeof(pe32);
  if (!Process32First(snapshot, &pe32)) {
    CloseHandle(snapshot);
    return -1;
  }
  do {
    if (pe32.th32ProcessID == pid) {
      ppid = pe32.th32ParentProcessID;
      break;
    }
  } while (Process32Next(snapshot, &pe32));
  CloseHandle(snapshot);
  return ppid;
}

static bool getProcessName(int pid, char *buffer, int max_len)
{
  bool result = false;
  HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (handle) {
    GetModuleFileNameEx(handle, 0, buffer, max_len);
    result = true;
  }
  CloseHandle(handle);
  return result;
}

static bool isStartedFromCommandPrompt()
{
  HWND hwnd = GetConsoleWindow();

  if (hwnd) {
    DWORD pid = (DWORD)-1;
    DWORD ppid = GetParentProcessID();
    char parent_name[MAX_PATH];
    bool start_from_launcher = false;

    GetWindowThreadProcessId(hwnd, &pid);
    if (getProcessName(ppid, parent_name, sizeof(parent_name))) {
      char *filename = strrchr(parent_name, '\\');
      if (filename != NULL) {
        start_from_launcher = strstr(filename, "blender.exe") != NULL;
      }
    }

    /* When we're starting from a wrapper we need to compare with parent process ID. */
    if (pid != (start_from_launcher ? ppid : GetCurrentProcessId()))
      return true;
  }

  return false;
}

int GHOST_SystemWin32::toggleConsole(int action)
{
  HWND wnd = GetConsoleWindow();

  switch (action) {
    case 3:  // startup: hide if not started from command prompt
    {
      if (!isStartedFromCommandPrompt()) {
        ShowWindow(wnd, SW_HIDE);
        m_consoleStatus = 0;
      }
      break;
    }
    case 0:  // hide
      ShowWindow(wnd, SW_HIDE);
      m_consoleStatus = 0;
      break;
    case 1:  // show
      ShowWindow(wnd, SW_SHOW);
      if (!isStartedFromCommandPrompt()) {
        DeleteMenu(GetSystemMenu(wnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
      }
      m_consoleStatus = 1;
      break;
    case 2:  // toggle
      ShowWindow(wnd, m_consoleStatus ? SW_HIDE : SW_SHOW);
      m_consoleStatus = !m_consoleStatus;
      if (m_consoleStatus && !isStartedFromCommandPrompt()) {
        DeleteMenu(GetSystemMenu(wnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
      }
      break;
  }

  return m_consoleStatus;
}
