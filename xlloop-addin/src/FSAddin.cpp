/*******************************************************************************
* This program and the accompanying materials
* are made available under the terms of the Common Public License v1.0
* which accompanies this distribution, and is available at 
* http://www.eclipse.org/legal/cpl-v10.html
* 
* Contributors:
*     Peter Smith
*******************************************************************************/

#include "common/INI.h"
#include "common/Log.h"
#include "common/VTCodec.h"
#include "xll/XLUtil.h"
#include "xll/xlcall.h"
#include "functionserver/Protocol.h"
#include "functionserver/XLConverter.h"

// The DLL instance
static HINSTANCE g_hinstance = NULL;

// The INI file
static dictionary* g_ini = NULL;

// The protocol manager
static Protocol* g_protocol = NULL;

// INI keys
#define FS_HOSTNAME ":hostname"
#define FS_PORT ":port"
#define FS_ADDIN_NAME ":addin.name"
#define FS_FUNCTION_NAME ":function.name"
#define FS_INCLUDE_VOLATILE ":include.volatile"
#define FS_FUNCTION_NAME_VOLATILE ":function.name.volatile"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if(fdwReason == DLL_PROCESS_ATTACH) {
		// Store reference to handle for later use
		g_hinstance = hinstDLL;

		// Load our (optional) ini file
		g_ini = INI::LoadIniFile(hinstDLL);

		// Initialise the log
		Log::Init(hinstDLL, iniparser_getstr(g_ini, LOG_FILE), iniparser_getstr(g_ini, LOG_LEVEL));
	}

	// OK
	return 1;
}

#ifdef __cplusplus
extern "C" {  
#endif 

__declspec(dllexport) int WINAPI xlAutoOpen(void)
{
	static XLOPER xDLL;
	Excel4(xlGetName, &xDLL, 0);

	// Register execute function
	char* fsName = iniparser_getstr(g_ini, FS_FUNCTION_NAME);
	if(fsName == NULL) {
		fsName = "FS";
	}
	int res = XLUtil::RegisterFunction(&xDLL, "FSExecute", "RCPPPPPPPPPP", fsName, 
		NULL, "1", "General", NULL, NULL, NULL, NULL);

	// Register execute function (volatile version (if requested))
	char* inclVol = iniparser_getstr(g_ini, FS_INCLUDE_VOLATILE);
	if(inclVol != NULL && strcmp(inclVol, "true") == 0) {
		char* fsvName = iniparser_getstr(g_ini, FS_FUNCTION_NAME_VOLATILE);
		if(fsvName == NULL) {
			fsvName = "FSV";
		}
		res = XLUtil::RegisterFunction(&xDLL, "FSExecuteVolatile", "RCPPPPPPPPPP!", fsvName, 
			NULL, "1", "General", NULL, NULL, NULL, NULL);
	}

	// Free the XLL filename
	Excel4(xlFree, 0, 1, (LPXLOPER) &xDLL);

	// OK
	return 1;
}

__declspec(dllexport) int WINAPI xlAutoClose(void)
{

	// Disconnect from server
	if(g_protocol != NULL) {
		g_protocol->disconnect();
		delete g_protocol;
		g_protocol = NULL;
	}

	return 1;
}

__declspec(dllexport) LPXLOPER WINAPI xlAutoRegister(LPXLOPER pxName)
{
	static XLOPER xDLL, xRegId;
	xRegId.xltype = xltypeErr;
	xRegId.val.err = xlerrValue;
	
	return (LPXLOPER) &xRegId;
}

__declspec(dllexport) int WINAPI xlAutoAdd(void)
{
	return 1;
}

__declspec(dllexport) int WINAPI xlAutoRemove(void)
{
	return 1;
}

__declspec(dllexport) void WINAPI xlAutoFree(LPXLOPER px)
{
}

__declspec(dllexport) LPXLOPER WINAPI xlAddInManagerInfo(LPXLOPER xAction)
{
	static XLOPER xInfo, xIntAction, xIntType;
	xIntType.xltype = xltypeInt;
	xIntType.val.w = xltypeInt;
	xInfo.xltype = xltypeErr;
	xInfo.val.err = xlerrValue;

	Excel4(xlCoerce, &xIntAction, 2, xAction, &xIntType);

	// Set addin name
	if(xIntAction.val.w == 1) {
		xInfo.xltype = xltypeStr | xlbitXLFree;
		char* addinName = iniparser_getstr(g_ini, FS_ADDIN_NAME);
		if(addinName == NULL) {
			addinName = XLUtil::MakeExcelString("XLLoop v0.0.4");
		} else {
			addinName = XLUtil::MakeExcelString(addinName);
		}
		xInfo.val.str = addinName;
	} 

	return (LPXLOPER) &xInfo;
}

__declspec(dllexport) LPXLOPER WINAPI FSExecute(char* name, LPXLOPER v0, LPXLOPER v1, LPXLOPER v2, LPXLOPER v3, LPXLOPER v4, 
												LPXLOPER v5, LPXLOPER v6, LPXLOPER v7, LPXLOPER v8, LPXLOPER v9)
{
	// Create our protocol manager
	if(g_protocol == NULL) {
		char* hostname = iniparser_getstr(g_ini, FS_HOSTNAME);
		char* port = iniparser_getstr(g_ini, FS_PORT);
		g_protocol = new Protocol(hostname == NULL ? "localhost" : hostname, 
			port == NULL ? 5454 : atoi(port));
	}

	// Attempt connection
	if(g_protocol->connect()) {
		static XLOPER err;
		err.xltype = xltypeStr;
		err.val.str = " #Could not connect to server  ";
		return &err;
	}

	// Convert the args
	VTCollection* coll = new VTCollection;
	coll->add(XLConverter::ConvertX(v0));
	coll->add(XLConverter::ConvertX(v1));
	coll->add(XLConverter::ConvertX(v2));
	coll->add(XLConverter::ConvertX(v3));
	coll->add(XLConverter::ConvertX(v4));
	coll->add(XLConverter::ConvertX(v5));
	coll->add(XLConverter::ConvertX(v6));
	coll->add(XLConverter::ConvertX(v7));
	coll->add(XLConverter::ConvertX(v8));
	coll->add(XLConverter::ConvertX(v9));

	// Exec function
	Variant* res = g_protocol->executeFunction(name, coll);
	delete coll;

	// Check for error
	if(!g_protocol->isConnected()) {
		delete res;
		static XLOPER err;
		err.xltype = xltypeStr;
		err.val.str = " #Could not connect to server  ";
		return &err;
	}

	// Convert result
	LPXLOPER xres = XLConverter::ConvertV(res);
	delete res;

	return xres;
}

__declspec(dllexport) LPXLOPER WINAPI FSExecuteVolatile(char* name, LPXLOPER v0, LPXLOPER v1, LPXLOPER v2, LPXLOPER v3, 
														LPXLOPER v4, LPXLOPER v5, LPXLOPER v6, LPXLOPER v7, LPXLOPER v8, 
														LPXLOPER v9)
{
	// Just call off to main function (as this should have the same behaviour only volatile)
	return FSExecute(name, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9);
}

#ifdef __cplusplus
}
#endif