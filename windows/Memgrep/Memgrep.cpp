/*
memgrep a tool to grep accross processes on Windows

Released as open source by NCC Group Plc - http://www.nccgroup.com/

Developed by Ollie Whitehouse, ollie dot whitehouse at nccgroup dot com

https://github.com/nccgroup/memgrep

Released under AGPL see LICENSE for more information
*/

#include "stdafx.h"
#include "XGetopt.h"

// Globals
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
bool bDumpHex = false;

//
//
//
// Notes: http://druid.caughq.org/src/printhex.c
//
void printhex( unsigned char *buf, int size ) {
	int x, y;

	for( x=1; x<=size; x++ ) {

		if( x == 1 ) printf( "%04x  ", x-1 ); /* Print an offset line header */

		printf( "%02x ", buf[x-1] ); /* print the hex value */

		if( x % 8 == 0 ) printf( " " ); /* padding space at 8 and 16 bytes */

		if( x % 16 == 0 ) {
			/* We're at the end of a line of hex, print the printables */
			printf( " " );
			for( y = x - 15; y <= x; y++ ) {
				if( isprint( buf[y-1] ) ) printf( "%c", buf[y-1] ); /* if it's printable, print it */
				else printf( "." ); /* otherwise substitute a period */
				if( y % 8 == 0 ) printf( " " ); /* 8 byte padding space */
			} 
			if( x < size ) printf( "\n%04x  ", x ); /* Print an offset line header */
		}
	}
	x--;

	/* If we didn't end on a 16 byte boundary, print some placeholder spaces before printing ascii */
	if( x % 16 != 0 ) {
		for( y = x+1; y <= x + (16-(x % 16)); y++ ) {
			printf( "   " ); /* hex value placeholder spaces */
			if( y % 8 == 0 ) printf( " " ); /* 8 and 16 byte padding spaces */
		};

		/* print the printables */
		printf( " " );
		for( y = (x+1) - (x % 16); y <= x; y++ ) {
			if( isprint( buf[y-1] ) ) printf( "%c", buf[y-1] ); /* if it's printable, print it */
			else printf( "." ); /* otherwise substitute a period */
			if( y % 8 == 0 ) printf( " " ); /* 8 and 16 byte padding space */
		}
	}

	/* Done! */
	printf( "\n" );
}

#ifdef WIN64
void PrintMemInfo(MEMORY_BASIC_INFORMATION64 memMeminfo)
#endif
#ifdef WIN32
void PrintMemInfo(MEMORY_BASIC_INFORMATION memMeminfo)
#endif
{

	switch (memMeminfo.AllocationProtect)
	{
		case PAGE_EXECUTE:
			fprintf(stdout,"[  x  ]");
			break;
		case PAGE_EXECUTE_READ:
			fprintf(stdout,"[r x  ]");
			break;
		case PAGE_EXECUTE_READWRITE:
			fprintf(stdout,"[rwx  ]");
			break;
		case PAGE_EXECUTE_WRITECOPY:
			fprintf(stdout,"[ wxc ]");
			break;
		case PAGE_NOACCESS:
			fprintf(stdout,"[     ]");
			break;
		case PAGE_READONLY:
			fprintf(stdout,"[r    ]");
			break;
		case PAGE_READWRITE:
			fprintf(stdout,"[rw   ]");
			break;
		case PAGE_WRITECOPY:
			fprintf(stdout,"[ w c ]");
			break;
	}

	switch (memMeminfo.Type){
		case MEM_IMAGE:
			fprintf(stdout," - image\n");
			break;
		case MEM_MAPPED:
			fprintf(stdout," - mapped\n");
			break;
		case MEM_PRIVATE:
			fprintf(stdout," - private\n");
			break;
	}
}

//
// Function	: ReadAndGrep
// Role		: Reads the process memory into our address space then we search it...
// Notes	: 
//
#ifdef WIN64
void ReadAndGrep(SIZE_T szSize, ULONG_PTR lngAddress, HANDLE hProcess, char *strString, MEMORY_BASIC_INFORMATION64 memMeminfo)
#endif
#ifdef WIN32
void ReadAndGrep(SIZE_T szSize, ULONG_PTR lngAddress, HANDLE hProcess, char *strString, MEMORY_BASIC_INFORMATION memMeminfo)
#endif
{
	SIZE_T szBytesRead=0;
	unsigned char *strBuffer=(unsigned char *)VirtualAlloc(0,szSize+1024,MEM_COMMIT,PAGE_READWRITE);
	unsigned char *strBufferNow = strBuffer;
	unsigned char *strBufferEnd = (strBuffer + szSize) - (strlen(strString) +1);
	unsigned int intCounter =0;
	if(strBuffer==NULL) return;

	if(ReadProcessMemory(hProcess,(LPVOID)lngAddress,strBuffer,szSize,&szBytesRead)==0){
		if(GetLastError()!=299)	fprintf(stderr,"[!] Failed to read process memory %d at %p read %ld\n",GetLastError(),lngAddress,szBytesRead);
	} else {

		while(strBufferNow<strBufferEnd){
			
			//fprintf(stdout,"[i] Searching %p which is %ld big and ends at %p\n",strBufferNow,szSize+1024,strBufferEnd);
			if (memcmp(strString,strBufferNow,strlen(strString)) == 0){
				fprintf(stdout,"[*] Got hit for %s at %p",strString,lngAddress+intCounter);
				PrintMemInfo(memMeminfo);
				if(bDumpHex) printhex(strBufferNow,(int)strlen(strString));
			} else {
				bool bMatch = true;
				for(int intCount =0; intCount<strlen(strString) && strBufferEnd < strBufferEnd + strlen(strString) + 1;intCount+=2){
					if(strBufferNow[intCount] != strString[intCount] || strBufferNow[intCount+1] != 0x00){
						bMatch = false;
					}
				}
				if(bMatch) {
					fprintf(stdout,"[*] Got unicode hit for %s at %p",strString,lngAddress+intCounter);
					PrintMemInfo(memMeminfo);
					if(bDumpHex) printhex(strBufferNow,(int)(strlen(strString)*2)+2);
				}
			}
			
			strBufferNow++;
			intCounter++;
		}
	}

	VirtualFree(strBuffer,szSize,MEM_RELEASE);
}

//
// Function	: OpenAndGrep
// Role		: Open a process, enumerate mapped pages of memory and pass to grep routine
// Notes	: 
// 
void OpenAndGrep(bool bASCII, bool bUNICODE, char* strString, DWORD dwPID)
{
	DWORD dwRet, dwMods;
	HANDLE hProcess;
	HMODULE hModule[4096];
	char cProcess[MAX_PATH]; // Process name
	SYSTEM_INFO sysnfoSysNFO;
	BOOL bIsWow64=FALSE;
	BOOL bIsWow64Other=FALSE;
	DWORD dwRES=0;
	
	hProcess = OpenProcess(PROCESS_ALL_ACCESS |PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
	if (hProcess == NULL)
	{
		if(GetLastError()==5){
			hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
			if (hProcess == NULL){
				fprintf(stderr,"[!] OpenProcess(%d),%d\n", dwPID, GetLastError());
				return;
			}
		} else {
			fprintf(stderr,"[!] OpenProcess(%d),%d\n", dwPID, GetLastError());
			return;
		}
	}

	if (EnumProcessModules(hProcess,hModule,4096*sizeof(HMODULE), &dwRet) == 0)
	{
		if(GetLastError() == 299){
			fprintf(stderr,"[i] 64bit process and we're 32bit - sad panda! skipping PID %d\n",dwPID);
		} else {
			fprintf(stderr,"[!] OpenAndGrep(%d),%d\n", dwPID, GetLastError());
		}
		return;
	}
	dwMods = dwRet / sizeof(HMODULE);

	GetModuleBaseName(hProcess,hModule[0],cProcess,MAX_PATH);

	if(IsWow64Process(GetCurrentProcess(),&bIsWow64)){
		GetNativeSystemInfo(&sysnfoSysNFO);
		
		if(bIsWow64)
		{
			//fwprintf(stdout,L"[i] Running under WOW64 - Page Size %d\n",sysnfoSysNFO.dwPageSize);
		} 
		else 
		{
			//fwprintf(stdout,L"[i] Not running under WOW64 - Page Size %d\n",sysnfoSysNFO.dwPageSize);	
		}
	} else {
		fwprintf(stdout,L"[!] Errot\n");
		return;
	}

	fprintf(stdout,"[i] Searching %s\n",cProcess);

	//
	// Walk the processes address space
	//
	unsigned char *pString = NULL;
	
	ULONG_PTR addrCurrent = 0;
    ULONG_PTR lastBase = (-1);

    for(;;)
    {
#ifdef WIN64
		MEMORY_BASIC_INFORMATION64 memMeminfo;
#endif
#ifdef WIN32
		MEMORY_BASIC_INFORMATION memMeminfo;
#endif
        VirtualQueryEx(hProcess, reinterpret_cast<LPVOID>(addrCurrent), reinterpret_cast<PMEMORY_BASIC_INFORMATION>(&memMeminfo), sizeof(memMeminfo) );

        if(lastBase == (ULONG_PTR) memMeminfo.BaseAddress) {
            break;
        }

        lastBase = (ULONG_PTR) memMeminfo.BaseAddress;

        if(memMeminfo.State == MEM_COMMIT) {
            //fprintf(stdout,"[i] %p\n", memMeminfo.BaseAddress);
			//fprintf(stdout,"[i] %ld\n", memMeminfo.RegionSize);
			ReadAndGrep(memMeminfo.RegionSize,(ULONG_PTR) memMeminfo.BaseAddress,hProcess,strString,memMeminfo);
        }

        addrCurrent += memMeminfo.RegionSize;
    }
}

//
// Function	: EnumerateProcesses
// Role		: Basic processes running
// Notes	: 
// 
void EnumerateProcesses(bool bASCII, bool bUNICODE, char* strString)
{
	DWORD dwPIDArray[2048], dwRet, dwPIDS, intCount;


	if (EnumProcesses(dwPIDArray,2048*sizeof(DWORD),&dwRet) == 0)
	{
		fprintf(stderr,"[!]  EnumProcesses(),%d\n", GetLastError());
		return;
	}

	dwPIDS = dwRet / sizeof(DWORD);

	for(intCount=0;intCount<dwPIDS;intCount++)
	{
		if(dwPIDArray[intCount] != GetCurrentProcessId()){
			OpenAndGrep(bASCII,bUNICODE,strString,dwPIDArray[intCount]);
		} else {
			fprintf(stdout,"[i] Skipping myself\n");
		}
	}
}

//
// Function	: SetDebugPrivilege
// Role		: Gets debug privs for our process
// Notes	: 
//
BOOL SetDebugPrivilege(HANDLE hProcess)
{
	LUID luid ;
	TOKEN_PRIVILEGES privs ;
	HANDLE hToken = NULL ;
	DWORD dwBufLen = 0 ;
	char buf[1024] ;
	
	ZeroMemory( &luid,sizeof(luid) ) ;
	
	if(! LookupPrivilegeValue( NULL, SE_DEBUG_NAME, &luid ))
		return false ;
	
	privs.PrivilegeCount = 1 ;
	privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED ;
	memcpy( &privs.Privileges[0].Luid, &luid, sizeof(privs.Privileges[0].Luid )
		) ;
	
	
	if( ! OpenProcessToken( hProcess, TOKEN_ALL_ACCESS,&hToken))
		return false ;
	
	if( !AdjustTokenPrivileges( hToken, FALSE, &privs,
		sizeof(buf),(PTOKEN_PRIVILEGES)buf, &dwBufLen ) )
		return false ;

	CloseHandle(hProcess);
	CloseHandle(hToken);
	
	return true ;
}


//
// Function	: PrintHelp
// Role		: 
// Notes	: 
// 
void PrintHelp(char *strExe){

	fprintf (stdout,"    i.e. %s -s <string>\n",strExe);
	fprintf (stdout,"    -s <string> to search for\n");
	fprintf (stdout,"    -x dump hex\n");
	fprintf (stdout,"\n");
	ExitProcess(1);
}

//
// Function	: _tmain
// Role		: Entry point
// Notes	: 
// 

int _tmain(int argc, _TCHAR* argv[])
{
	char	chOpt;
	char	*strString = NULL;

	// Extract all the options
	while ((chOpt = getopt(argc, argv, _T("s:xh"))) != EOF) 
	switch(chOpt)
	{
		case _T('s'):
			strString = optarg;
			break;
		case _T('x'):
			bDumpHex = true;
			break;
		default:
			fwprintf(stderr,L"[!] No handler - %c\n", chOpt);
			break;
	}

	if(strString == NULL){
		PrintHelp(argv[0]);
		return -1;
	}

	SetDebugPrivilege(GetCurrentProcess());
	EnumerateProcesses(true,true,strString);
	return 0;
}
