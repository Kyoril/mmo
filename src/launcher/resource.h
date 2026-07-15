//{{NO_DEPENDENCIES}}
// Microsoft Visual C++ generated include file.
// Used by launcher.rc
//
#define IDD_DIALOG1                     101
#define IDI_ICON1                       102
#define IDB_BITMAP1                     103
#define IDC_PROGRESS_BAR                1001
#define IDC_CLOSE                       1002
#define IDC_PLAY                        1003
#define IDC_STATUS_LABEL                1004
#define IDC_OVERALL                     1005
#define IDC_TOTAL                       1005
#define IDC_CURRENT                     1006
#define IDC_CREATE_SHORTCUT             1007
#define IDC_IMAGE                       1008
#define IDC_STATIC                      -1

// Resource identifiers for the reworked launcher UI. Ranges are reserved by type so
// that an id alone identifies its kind, both for the Win32 RCDATA lookup and for the
// macOS bundle name table in resource_names.h.
//
// NOTE: adding an entry here requires a matching line in launcher.rc, a matching
// entry in resource_names.h, and a matching path in the launcherAssets list in
// CMakeLists.txt. Nothing parses .rc includes, so that list is what makes touching an
// asset actually relink.

// --- Embedded images (RCDATA, PNG): 300-399 ---------------------------------
#define IDR_PNG_SPLASH                  300

// --- Embedded fonts (RCDATA, TTF): 400-499 ----------------------------------
#define IDR_TTF_DISPLAY                 400
#define IDR_TTF_BODY                    401

// Next default values for new objects
// 
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE        104
#define _APS_NEXT_COMMAND_VALUE         40001
#define _APS_NEXT_CONTROL_VALUE         1009
#define _APS_NEXT_SYMED_VALUE           101
#endif
#endif
