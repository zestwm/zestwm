/* Protocol constants from X11/X.h (resource IDs, masks, enums). No Xlib types. */
#pragma once

#include <cstdint>

#ifndef None
inline constexpr long None = 0L; /* universal null resource or null atom */
#endif

inline constexpr long ParentRelative = 1L; /* background pixmap in CreateWindow and ChangeWindowAttributes */

inline constexpr long CopyFromParent = 0L; /* border pixmap in CreateWindow/ChangeWindowAttributes, special VisualID/window class in CreateWindow */

inline constexpr long PointerWindow = 0L; /* destination window in SendEvent */
inline constexpr long InputFocus    = 1L; /* destination window in SendEvent */

inline constexpr long PointerRoot = 1L; /* focus window in SetInputFocus */

inline constexpr long AnyPropertyType = 0L; /* special Atom, passed to GetProperty */

inline constexpr long AnyKey = 0L; /* special Key Code, passed to GrabKey */

inline constexpr long AnyButton = 0L; /* special Button Code, passed to GrabButton */

inline constexpr long AllTemporary = 0L; /* special Resource ID passed to KillClient */

inline constexpr long CurrentTime = 0L; /* special Time */

inline constexpr long NoSymbol = 0L; /* special KeySym */

/*****************************************************************
 * EVENT DEFINITIONS
 *****************************************************************/

/* Input Event Masks. Used as event-mask window attribute and as arguments
   to Grab requests.  Not to be confused with event names.  */

inline constexpr long NoEventMask              = 0L;
inline constexpr long KeyPressMask             = (1L << 0);
inline constexpr long KeyReleaseMask           = (1L << 1);
inline constexpr long ButtonPressMask          = (1L << 2);
inline constexpr long ButtonReleaseMask        = (1L << 3);
inline constexpr long EnterWindowMask          = (1L << 4);
inline constexpr long LeaveWindowMask          = (1L << 5);
inline constexpr long PointerMotionMask        = (1L << 6);
inline constexpr long PointerMotionHintMask    = (1L << 7);
inline constexpr long Button1MotionMask        = (1L << 8);
inline constexpr long Button2MotionMask        = (1L << 9);
inline constexpr long Button3MotionMask        = (1L << 10);
inline constexpr long Button4MotionMask        = (1L << 11);
inline constexpr long Button5MotionMask        = (1L << 12);
inline constexpr long ButtonMotionMask         = (1L << 13);
inline constexpr long KeymapStateMask          = (1L << 14);
inline constexpr long ExposureMask             = (1L << 15);
inline constexpr long VisibilityChangeMask     = (1L << 16);
inline constexpr long StructureNotifyMask      = (1L << 17);
inline constexpr long ResizeRedirectMask       = (1L << 18);
inline constexpr long SubstructureNotifyMask   = (1L << 19);
inline constexpr long SubstructureRedirectMask = (1L << 20);
inline constexpr long FocusChangeMask          = (1L << 21);
inline constexpr long PropertyChangeMask       = (1L << 22);
inline constexpr long ColormapChangeMask       = (1L << 23);
inline constexpr long OwnerGrabButtonMask      = (1L << 24);

/* Event names.  Used in "type" field in XEvent structures.  Not to be
confused with event masks above.  They start from 2 because 0 and 1
are reserved in the protocol for errors and replies. */

inline constexpr std::int32_t KeyPress         = 2;
inline constexpr std::int32_t KeyRelease       = 3;
inline constexpr std::int32_t ButtonPress      = 4;
inline constexpr std::int32_t ButtonRelease    = 5;
inline constexpr std::int32_t MotionNotify     = 6;
inline constexpr std::int32_t EnterNotify      = 7;
inline constexpr std::int32_t LeaveNotify      = 8;
inline constexpr std::int32_t FocusIn          = 9;
inline constexpr std::int32_t FocusOut         = 10;
inline constexpr std::int32_t KeymapNotify     = 11;
inline constexpr std::int32_t Expose           = 12;
inline constexpr std::int32_t GraphicsExpose   = 13;
inline constexpr std::int32_t NoExpose         = 14;
inline constexpr std::int32_t VisibilityNotify = 15;
inline constexpr std::int32_t CreateNotify     = 16;
inline constexpr std::int32_t DestroyNotify    = 17;
inline constexpr std::int32_t UnmapNotify      = 18;
inline constexpr std::int32_t MapNotify        = 19;
inline constexpr std::int32_t MapRequest       = 20;
inline constexpr std::int32_t ReparentNotify   = 21;
inline constexpr std::int32_t ConfigureNotify  = 22;
inline constexpr std::int32_t ConfigureRequest = 23;
inline constexpr std::int32_t GravityNotify    = 24;
inline constexpr std::int32_t ResizeRequest    = 25;
inline constexpr std::int32_t CirculateNotify  = 26;
inline constexpr std::int32_t CirculateRequest = 27;
inline constexpr std::int32_t PropertyNotify   = 28;
inline constexpr std::int32_t SelectionClear   = 29;
inline constexpr std::int32_t SelectionRequest = 30;
inline constexpr std::int32_t SelectionNotify  = 31;
inline constexpr std::int32_t ColormapNotify   = 32;
inline constexpr std::int32_t ClientMessage    = 33;
inline constexpr std::int32_t MappingNotify    = 34;
inline constexpr std::int32_t GenericEvent     = 35;
inline constexpr std::int32_t LASTEvent        = 36; /* must be bigger than any event # */

/* Key masks. Used as modifiers to GrabButton and GrabKey, results of QueryPointer,
   state in various key-, mouse-, and button-related events. */

inline constexpr std::int32_t ShiftMask   = (1 << 0);
inline constexpr std::int32_t LockMask    = (1 << 1);
inline constexpr std::int32_t ControlMask = (1 << 2);
inline constexpr std::int32_t Mod1Mask    = (1 << 3);
inline constexpr std::int32_t Mod2Mask    = (1 << 4);
inline constexpr std::int32_t Mod3Mask    = (1 << 5);
inline constexpr std::int32_t Mod4Mask    = (1 << 6);
inline constexpr std::int32_t Mod5Mask    = (1 << 7);

/* modifier names.  Used to build a SetModifierMapping request or
   to read a GetModifierMapping request.  These correspond to the
   masks defined above. */
inline constexpr std::int32_t ShiftMapIndex   = 0;
inline constexpr std::int32_t LockMapIndex    = 1;
inline constexpr std::int32_t ControlMapIndex = 2;
inline constexpr std::int32_t Mod1MapIndex    = 3;
inline constexpr std::int32_t Mod2MapIndex    = 4;
inline constexpr std::int32_t Mod3MapIndex    = 5;
inline constexpr std::int32_t Mod4MapIndex    = 6;
inline constexpr std::int32_t Mod5MapIndex    = 7;

/* button masks.  Used in same manner as Key masks above. Not to be confused
   with button names below. */

inline constexpr std::int32_t Button1Mask = (1 << 8);
inline constexpr std::int32_t Button2Mask = (1 << 9);
inline constexpr std::int32_t Button3Mask = (1 << 10);
inline constexpr std::int32_t Button4Mask = (1 << 11);
inline constexpr std::int32_t Button5Mask = (1 << 12);

inline constexpr std::int32_t AnyModifier = (1 << 15); /* used in GrabButton, GrabKey */

/* button names. Used as arguments to GrabButton and as detail in ButtonPress
   and ButtonRelease events.  Not to be confused with button masks above.
   Note that 0 is already defined above as "AnyButton".  */

inline constexpr std::int32_t Button1 = 1;
inline constexpr std::int32_t Button2 = 2;
inline constexpr std::int32_t Button3 = 3;
inline constexpr std::int32_t Button4 = 4;
inline constexpr std::int32_t Button5 = 5;

/* Notify modes */

inline constexpr std::int32_t NotifyNormal       = 0;
inline constexpr std::int32_t NotifyGrab         = 1;
inline constexpr std::int32_t NotifyUngrab       = 2;
inline constexpr std::int32_t NotifyWhileGrabbed = 3;

inline constexpr std::int32_t NotifyHint = 1; /* for MotionNotify events */

/* Notify detail */

inline constexpr std::int32_t NotifyAncestor         = 0;
inline constexpr std::int32_t NotifyVirtual          = 1;
inline constexpr std::int32_t NotifyInferior         = 2;
inline constexpr std::int32_t NotifyNonlinear        = 3;
inline constexpr std::int32_t NotifyNonlinearVirtual = 4;
inline constexpr std::int32_t NotifyPointer          = 5;
inline constexpr std::int32_t NotifyPointerRoot      = 6;
inline constexpr std::int32_t NotifyDetailNone       = 7;

/* Visibility notify */

inline constexpr std::int32_t VisibilityUnobscured        = 0;
inline constexpr std::int32_t VisibilityPartiallyObscured = 1;
inline constexpr std::int32_t VisibilityFullyObscured     = 2;

/* Circulation request */

inline constexpr std::int32_t PlaceOnTop    = 0;
inline constexpr std::int32_t PlaceOnBottom = 1;

/* protocol families */

inline constexpr std::int32_t FamilyInternet  = 0; /* IPv4 */
inline constexpr std::int32_t FamilyDECnet    = 1;
inline constexpr std::int32_t FamilyChaos     = 2;
inline constexpr std::int32_t FamilyInternet6 = 6; /* IPv6 */

/* authentication families not tied to a specific protocol */
inline constexpr std::int32_t FamilyServerInterpreted = 5;

/* Property notification */

inline constexpr std::int32_t PropertyNewValue = 0;
inline constexpr std::int32_t PropertyDelete   = 1;

/* Color Map notification */

inline constexpr std::int32_t ColormapUninstalled = 0;
inline constexpr std::int32_t ColormapInstalled   = 1;

/* GrabPointer, GrabButton, GrabKeyboard, GrabKey Modes */

inline constexpr std::int32_t GrabModeSync  = 0;
inline constexpr std::int32_t GrabModeAsync = 1;

/* GrabPointer, GrabKeyboard reply status */

inline constexpr std::int32_t GrabSuccess     = 0;
inline constexpr std::int32_t AlreadyGrabbed  = 1;
inline constexpr std::int32_t GrabInvalidTime = 2;
inline constexpr std::int32_t GrabNotViewable = 3;
inline constexpr std::int32_t GrabFrozen      = 4;

/* AllowEvents modes */

inline constexpr std::int32_t AsyncPointer   = 0;
inline constexpr std::int32_t SyncPointer    = 1;
inline constexpr std::int32_t ReplayPointer  = 2;
inline constexpr std::int32_t AsyncKeyboard  = 3;
inline constexpr std::int32_t SyncKeyboard   = 4;
inline constexpr std::int32_t ReplayKeyboard = 5;
inline constexpr std::int32_t AsyncBoth      = 6;
inline constexpr std::int32_t SyncBoth       = 7;

/* Used in SetInputFocus, GetInputFocus */

inline constexpr std::int32_t RevertToNone        = static_cast<std::int32_t>(None);
inline constexpr std::int32_t RevertToPointerRoot = static_cast<std::int32_t>(PointerRoot);
inline constexpr std::int32_t RevertToParent      = 2;

/*****************************************************************
 * ERROR CODES
 *****************************************************************/

inline constexpr std::int32_t Success           = 0;  /* everything's okay */
inline constexpr std::int32_t BadRequest        = 1;  /* bad request code */
inline constexpr std::int32_t BadValue          = 2;  /* int parameter out of range */
inline constexpr std::int32_t BadWindow         = 3;  /* parameter not a Window */
inline constexpr std::int32_t BadPixmap         = 4;  /* parameter not a Pixmap */
inline constexpr std::int32_t BadAtom           = 5;  /* parameter not an Atom */
inline constexpr std::int32_t BadCursor         = 6;  /* parameter not a Cursor */
inline constexpr std::int32_t BadFont           = 7;  /* parameter not a Font */
inline constexpr std::int32_t BadMatch          = 8;  /* parameter mismatch */
inline constexpr std::int32_t BadDrawable       = 9;  /* parameter not a Pixmap or Window */
inline constexpr std::int32_t BadAccess         = 10; /* context dependent access violation */
inline constexpr std::int32_t BadAlloc          = 11; /* insufficient resources */
inline constexpr std::int32_t BadColor          = 12; /* no such colormap */
inline constexpr std::int32_t BadGC             = 13; /* parameter not a GC */
inline constexpr std::int32_t BadIDChoice       = 14; /* choice not in range or already used */
inline constexpr std::int32_t BadName           = 15; /* font or color name doesn't exist */
inline constexpr std::int32_t BadLength         = 16; /* Request length incorrect */
inline constexpr std::int32_t BadImplementation = 17; /* server is defective */

inline constexpr std::int32_t FirstExtensionError = 128;
inline constexpr std::int32_t LastExtensionError  = 255;

/*****************************************************************
 * WINDOW DEFINITIONS
 *****************************************************************/

/* Window classes used by CreateWindow */
/* Note that CopyFromParent is already defined as 0 above */

inline constexpr std::int32_t InputOutput = 1;
inline constexpr std::int32_t InputOnly   = 2;

/* Window attributes for CreateWindow and ChangeWindowAttributes */

inline constexpr long CWBackPixmap       = (1L << 0);
inline constexpr long CWBackPixel        = (1L << 1);
inline constexpr long CWBorderPixmap     = (1L << 2);
inline constexpr long CWBorderPixel      = (1L << 3);
inline constexpr long CWBitGravity       = (1L << 4);
inline constexpr long CWWinGravity       = (1L << 5);
inline constexpr long CWBackingStore     = (1L << 6);
inline constexpr long CWBackingPlanes    = (1L << 7);
inline constexpr long CWBackingPixel     = (1L << 8);
inline constexpr long CWOverrideRedirect = (1L << 9);
inline constexpr long CWSaveUnder        = (1L << 10);
inline constexpr long CWEventMask        = (1L << 11);
inline constexpr long CWDontPropagate    = (1L << 12);
inline constexpr long CWColormap         = (1L << 13);
inline constexpr long CWCursor           = (1L << 14);

/* ConfigureWindow structure */

inline constexpr std::int32_t CWX           = (1 << 0);
inline constexpr std::int32_t CWY           = (1 << 1);
inline constexpr std::int32_t CWWidth       = (1 << 2);
inline constexpr std::int32_t CWHeight      = (1 << 3);
inline constexpr std::int32_t CWBorderWidth = (1 << 4);
inline constexpr std::int32_t CWSibling     = (1 << 5);
inline constexpr std::int32_t CWStackMode   = (1 << 6);

/* Bit Gravity */

inline constexpr std::int32_t ForgetGravity    = 0;
inline constexpr std::int32_t NorthWestGravity = 1;
inline constexpr std::int32_t NorthGravity     = 2;
inline constexpr std::int32_t NorthEastGravity = 3;
inline constexpr std::int32_t WestGravity      = 4;
inline constexpr std::int32_t CenterGravity    = 5;
inline constexpr std::int32_t EastGravity      = 6;
inline constexpr std::int32_t SouthWestGravity = 7;
inline constexpr std::int32_t SouthGravity     = 8;
inline constexpr std::int32_t SouthEastGravity = 9;
inline constexpr std::int32_t StaticGravity    = 10;

/* Window gravity + bit gravity above */

inline constexpr std::int32_t UnmapGravity = 0;

/* Used in CreateWindow for backing-store hint */

inline constexpr std::int32_t NotUseful  = 0;
inline constexpr std::int32_t WhenMapped = 1;
inline constexpr std::int32_t Always     = 2;

/* Used in GetWindowAttributes reply */

inline constexpr std::int32_t IsUnmapped   = 0;
inline constexpr std::int32_t IsUnviewable = 1;
inline constexpr std::int32_t IsViewable   = 2;

/* Used in ChangeSaveSet */

inline constexpr std::int32_t SetModeInsert = 0;
inline constexpr std::int32_t SetModeDelete = 1;

/* Used in ChangeCloseDownMode */

inline constexpr std::int32_t DestroyAll      = 0;
inline constexpr std::int32_t RetainPermanent = 1;
inline constexpr std::int32_t RetainTemporary = 2;

/* Window stacking method (in configureWindow) */

inline constexpr std::int32_t Above    = 0;
inline constexpr std::int32_t Below    = 1;
inline constexpr std::int32_t TopIf    = 2;
inline constexpr std::int32_t BottomIf = 3;
inline constexpr std::int32_t Opposite = 4;

/* Property modes */

inline constexpr std::int32_t PropModeReplace = 0;
inline constexpr std::int32_t PropModePrepend = 1;
inline constexpr std::int32_t PropModeAppend  = 2;
