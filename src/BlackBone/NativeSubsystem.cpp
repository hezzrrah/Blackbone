#include "NativeSubsystem.h"
#include "Utils.h"
#include "DynImport.h"
#include "Macro.h"

#include <type_traits>
#include <Psapi.h>

namespace blackbone
{

Native::Native( HANDLE hProcess, bool x86OS /*= false*/ )
    : _hProcess( hProcess )
{
    SYSTEM_INFO info = { 0 };
    GetNativeSystemInfo( &info );
    _pageSize = info.dwPageSize;

    // x86 OS, emulate WOW64 processes
    if (x86OS)
    {
        _wowBarrier.sourceWow64 = true;
        _wowBarrier.targetWow64 = true;
        _wowBarrier.type = wow_32_32;
        _wowBarrier.x86OS = true;
    }
    else
    {
        BOOL wowSrc = FALSE, wowTgt = FALSE;
        IsWow64Process( GetCurrentProcess(), &wowSrc );
        IsWow64Process( _hProcess, &wowTgt );

        _wowBarrier.sourceWow64 = (wowSrc == TRUE);
        _wowBarrier.targetWow64 = (wowTgt == TRUE);

        if (wowSrc == TRUE && wowTgt == TRUE)
            _wowBarrier.type = wow_32_32;
        else if (wowSrc == FALSE && wowTgt == FALSE)
            _wowBarrier.type = wow_64_64;
        else if (wowSrc == TRUE)
            _wowBarrier.type = wow_32_64;
        else
            _wowBarrier.type = wow_64_32;
    }

    HMODULE hNtdll = GetModuleHandleW( L"ntdll.dll" );

    DynImport::load( "NtQueryInformationProcess", hNtdll );
    DynImport::load( "NtQueryInformationThread",  hNtdll );
    DynImport::load( "NtQueryObject",             hNtdll );  
    DynImport::load( "NtQueryVirtualMemory",      hNtdll );
    DynImport::load( "NtCreateThreadEx",          hNtdll );
    DynImport::load( "NtLockVirtualMemory",       hNtdll );
}

/*
*/
Native::~Native()
{
}

/// <summary>
/// Allocate virtual memory
/// </summary>
/// <param name="lpAddress">Allocation address</param>
/// <param name="dwSize">Region size</param>
/// <param name="flAllocationType">Allocation type</param>
/// <param name="flProtect">Memory protection</param>
/// <returns>Status code</returns>
NTSTATUS Native::VirualAllocExT( ptr_t& lpAddress, size_t dwSize, DWORD flAllocationType, DWORD flProtect )
{
    LastNtStatus( STATUS_SUCCESS );
    lpAddress = reinterpret_cast<ptr_t>
        (VirtualAllocEx( _hProcess, reinterpret_cast<LPVOID>(lpAddress), dwSize, flAllocationType, flProtect ));

    return LastNtStatus();
}


/// <summary>
/// Free virtual memory
/// </summary>
/// <param name="lpAddress">Memory address</param>
/// <param name="dwSize">Region size</param>
/// <param name="dwFreeType">Memory release type.</param>
/// <returns>Status code</returns>
NTSTATUS Native::VirualFreeExT( ptr_t lpAddress, size_t dwSize, DWORD dwFreeType )
{
    LastNtStatus( STATUS_SUCCESS );
    VirtualFreeEx( _hProcess, reinterpret_cast<LPVOID>(lpAddress), dwSize, dwFreeType );
    return LastNtStatus();
}

/// <summary>
/// Query virtual memory
/// </summary>
/// <param name="lpAddress">Address to query</param>
/// <param name="lpBuffer">Retrieved memory info</param>
/// <returns>Status code</returns>
NTSTATUS Native::VirtualQueryExT( ptr_t lpAddress, PMEMORY_BASIC_INFORMATION64 lpBuffer )
{
    LastNtStatus( STATUS_SUCCESS );
    VirtualQueryEx( _hProcess, reinterpret_cast<LPCVOID>(lpAddress), 
                    reinterpret_cast<PMEMORY_BASIC_INFORMATION>(lpBuffer), 
                    sizeof(MEMORY_BASIC_INFORMATION) );

    return LastNtStatus();
}

/// <summary>
/// Query virtual memory
/// </summary>
/// <param name="lpAddress">Address to query</param>
/// <param name="lpBuffer">Retrieved memory info</param>
/// <returns>Status code</returns>
NTSTATUS Native::VirtualQueryExT( ptr_t lpAddress, MEMORY_INFORMATION_CLASS infoClass, LPVOID lpBuffer, size_t bufSize )
{
    SIZE_T retLen = 0;

    LastNtStatus( STATUS_SUCCESS );
    
    return GET_IMPORT( NtQueryVirtualMemory )( _hProcess, reinterpret_cast<LPVOID>(lpAddress), 
                                               infoClass, lpBuffer, bufSize, &retLen );
}

/// <summary>
/// Change memory protection
/// </summary>
/// <param name="lpAddress">Memory address.</param>
/// <param name="dwSize">Region size</param>
/// <param name="flProtect">New protection.</param>
/// <param name="flOld">Old protection</param>
/// <returns>Status code</returns>
NTSTATUS Native::VirtualProtectExT( ptr_t lpAddress, DWORD64 dwSize, DWORD flProtect, DWORD* flOld )
{
    DWORD junk = 0;
    if (!flOld)
        flOld = &junk;

    LastNtStatus( STATUS_SUCCESS );

    VirtualProtectEx( _hProcess, reinterpret_cast<LPVOID>(lpAddress), static_cast<SIZE_T>(dwSize), flProtect, flOld );

    return LastNtStatus();
}

/// <summary>
/// Read virtual memory
/// </summary>
/// <param name="lpBaseAddress">Memory address</param>
/// <param name="lpBuffer">Output buffer</param>
/// <param name="nSize">Number of bytes to read</param>
/// <param name="lpBytes">Mumber of bytes read</param>
/// <returns>Status code</returns>
NTSTATUS Native::ReadProcessMemoryT( ptr_t lpBaseAddress, LPVOID lpBuffer, size_t nSize, DWORD64 *lpBytes /*= nullptr */ )
{
    LastNtStatus( STATUS_SUCCESS );
    ReadProcessMemory( _hProcess, reinterpret_cast<LPVOID>(lpBaseAddress), lpBuffer, nSize, reinterpret_cast<SIZE_T*>(lpBytes) );
    return LastNtStatus();
}

/// <summary>
/// Write virtual memory
/// </summary>
/// <param name="lpBaseAddress">Memory address</param>
/// <param name="lpBuffer">Buffer to write</param>
/// <param name="nSize">Number of bytes to read</param>
/// <param name="lpBytes">Mumber of bytes read</param>
/// <returns>Status code</returns>
NTSTATUS Native::WriteProcessMemoryT( ptr_t lpBaseAddress, LPCVOID lpBuffer, size_t nSize, DWORD64 *lpBytes /*= nullptr */ )
{
    LastNtStatus( STATUS_SUCCESS );
    WriteProcessMemory( _hProcess, reinterpret_cast<LPVOID>(lpBaseAddress), lpBuffer, nSize, reinterpret_cast<SIZE_T*>(lpBytes) );
    return LastNtStatus();
}

/// <summary>
/// Creates new thread in the remote process
/// </summary>
/// <param name="hThread">Created thread handle</param>
/// <param name="entry">Thread entry point</param>
/// <param name="arg">Thread argument</param>
/// <param name="flags">Creation flags</param>
/// <returns>Status code</returns>
NTSTATUS Native::CreateRemoteThreadT( HANDLE& hThread, ptr_t entry, ptr_t arg, DWORD flags )
{
    LastNtStatus( STATUS_SUCCESS );
    NTSTATUS status = 0; 
    auto pCreateThread = GET_IMPORT( NtCreateThreadEx );

    if (pCreateThread)
    {
        OBJECT_ATTRIBUTES ob = { 0 };
        ob.Length = sizeof(ob);
        BOOLEAN bSuspend = (flags & CREATE_SUSPENDED) ? TRUE : FALSE;

        status = pCreateThread( &hThread, THREAD_ALL_ACCESS, &ob,
                                _hProcess, reinterpret_cast<PTHREAD_START_ROUTINE>(entry),
                                reinterpret_cast<LPVOID>(arg), bSuspend,
                                0, 0x1000, 0x100000, NULL );
    }
    else
    {
        hThread = CreateRemoteThread( _hProcess, NULL, 0, reinterpret_cast<PTHREAD_START_ROUTINE>(entry),
                                      reinterpret_cast<LPVOID>(arg), flags, NULL );
        status = LastNtStatus();
    }

    return status;
}

/// <summary>
/// Get native thread context
/// </summary>
/// <param name="hThread">Thread handle.</param>
/// <param name="ctx">Thread context</param>
/// <returns>Status code</returns>
NTSTATUS Native::GetThreadContextT( HANDLE hThread, _CONTEXT64& ctx )
{
    LastNtStatus( STATUS_SUCCESS );
    GetThreadContext( hThread, reinterpret_cast<PCONTEXT>(&ctx) );
    return LastNtStatus();
}

/// <summary>
/// Get WOW64 thread context
/// </summary>
/// <param name="hThread">Thread handle.</param>
/// <param name="ctx">Thread context</param>
/// <returns>Status code</returns>
NTSTATUS Native::GetThreadContextT( HANDLE hThread, _CONTEXT32& ctx )
{
    // Target process is x64. WOW64 CONTEXT is not available.
    if (_wowBarrier.targetWow64 == false)
    {
        return 0;
    }
    else
    {
        LastNtStatus( STATUS_SUCCESS );
        Wow64GetThreadContext( hThread, reinterpret_cast<PWOW64_CONTEXT>(&ctx) );
        return LastNtStatus();
    }
}

/// <summary>
/// Set native thread context
/// </summary>
/// <param name="hThread">Thread handle.</param>
/// <param name="ctx">Thread context</param>
/// <returns>Status code</returns>
NTSTATUS Native::SetThreadContextT( HANDLE hThread, _CONTEXT64& ctx )
{
    LastNtStatus( STATUS_SUCCESS );
    SetThreadContext( hThread, reinterpret_cast<PCONTEXT>(&ctx) );
    return LastNtStatus();
}

/// <summary>
/// Set WOW64 thread context
/// </summary>
/// <param name="hThread">Thread handle.</param>
/// <param name="ctx">Thread context</param>
/// <returns>Status code</returns>
NTSTATUS Native::SetThreadContextT( HANDLE hThread, _CONTEXT32& ctx )
{
    // Target process is x64. 32bit CONTEXT is not available.
    if (_wowBarrier.targetWow64 == false)
    {
        return 0;
    }
    else
    {
        LastNtStatus( STATUS_SUCCESS );
        Wow64SetThreadContext( hThread, reinterpret_cast<PWOW64_CONTEXT>(&ctx) );
        return LastNtStatus();
    }
}

/// <summary>
/// Get WOW64 PEB
/// </summary>
/// <param name="ppeb">Retrieved PEB</param>
/// <returns>PEB pointer</returns>
ptr_t Native::getPEB( _PEB32* ppeb )
{
    // Target process is x64. PEB32 is not available.
    if (_wowBarrier.targetWow64 == false)
    {
        return 0;
    }
    else
    {
        ptr_t ptr = 0;
        if(GET_IMPORT( NtQueryInformationProcess )(_hProcess, ProcessWow64Information, &ptr, sizeof(ptr), NULL) == STATUS_SUCCESS)
            ReadProcessMemory( _hProcess, reinterpret_cast<LPCVOID>(ptr), ppeb, sizeof(_PEB32), NULL );

        return ptr;
    }
}

/// <summary>
/// Get native PEB
/// </summary>
/// <param name="ppeb">Retrieved PEB</param>
/// <returns>PEB pointer</returns>
ptr_t Native::getPEB( _PEB64* ppeb )
{
    PROCESS_BASIC_INFORMATION pbi = { 0 };
    ULONG bytes = 0;

    if (GET_IMPORT( NtQueryInformationProcess )( _hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &bytes ) == STATUS_SUCCESS)
        if (ppeb)
            ReadProcessMemory( _hProcess, pbi.PebBaseAddress, ppeb, sizeof(_PEB32), NULL );

    return reinterpret_cast<ptr_t>(pbi.PebBaseAddress);
}

/// <summary>
/// Get WOW64 TEB
/// </summary>
/// <param name="ppeb">Retrieved TEB</param>
/// <returns>TEB pointer</returns>
ptr_t Native::getTEB( HANDLE hThread, _TEB32* pteb )
{
    // Target process is x64. TEB32 is not available.
    if (_wowBarrier.targetWow64 == false)
    {
        return 0;
    }
    // Retrieving TEB32 from x64 process.
    else
    {
        _THREAD_BASIC_INFORMATION_T<DWORD64> tbi = { 0 };
        ULONG bytes = 0;

        if (GET_IMPORT( NtQueryInformationThread )( hThread, (THREADINFOCLASS)0, &tbi, sizeof(tbi), &bytes ) == STATUS_SUCCESS)
            if (pteb)
                ReadProcessMemory( _hProcess, (const uint8_t*)tbi.TebBaseAddress + 0x2000, pteb, sizeof(_TEB32), NULL );

        return tbi.TebBaseAddress + 0x2000;
    }

}

/// <summary>
/// Get native TEB
/// </summary>
/// <param name="ppeb">Retrieved TEB</param>
/// <returns>TEB pointer</returns>
ptr_t Native::getTEB( HANDLE hThread, _TEB64* pteb )
{
    _THREAD_BASIC_INFORMATION_T<DWORD64> tbi = { 0 };
    ULONG bytes = 0;

    if (GET_IMPORT( NtQueryInformationThread )( hThread, (THREADINFOCLASS)0, &tbi, sizeof(tbi), &bytes ) == STATUS_SUCCESS)
        if (pteb)
            ReadProcessMemory( _hProcess, reinterpret_cast<LPCVOID>(tbi.TebBaseAddress), pteb, sizeof(_TEB64), NULL );

    return tbi.TebBaseAddress;
}

/// <summary>
/// Enumerate process modules
/// </summary>
/// <param name="result">Found modules</param>
/// <returns>Module count</returns>
template<typename T>
size_t Native::EnumModulesT( Native::listModules& result )
{
    _PEB_T2<T>::type peb = { 0 };
    _PEB_LDR_DATA2<T> ldr = { 0 };

    result.clear();

    if (getPEB( &peb ) != 0 && ReadProcessMemoryT( peb.Ldr, &ldr, sizeof(ldr), 0 ) == STATUS_SUCCESS)
    {
        for (T head = ldr.InLoadOrderModuleList.Flink;
              head != (peb.Ldr + FIELD_OFFSET( _PEB_LDR_DATA2<T>, InLoadOrderModuleList ));
              ReadProcessMemoryT( static_cast<ptr_t>(head), &head, sizeof(head), 0 ))
        {
            ModuleData data;
            wchar_t localPath[512] = { 0 };
            _LDR_DATA_TABLE_ENTRY_BASE<T> localdata = { 0 };

            ReadProcessMemoryT( head, &localdata, sizeof(localdata), 0 );
            ReadProcessMemoryT( localdata.FullDllName.Buffer, localPath, localdata.FullDllName.Length, 0 );

            data.baseAddress = localdata.DllBase;
            data.size = localdata.SizeOfImage;
            data.fullPath = Utils::ToLower( localPath );
            data.name = Utils::StripPath( data.fullPath );
            data.manual = false;
            data.type = std::is_same<T, DWORD>::value ? mt_mod32 : mt_mod64;

            result.emplace_back( data );
        }
    }

    return result.size();
}

/// <summary>
/// Enum process section objects
/// </summary>
/// <param name="result">Found modules</param>
/// <returns>Sections count</returns>
size_t Native::EnumSections( listModules& result )
{
    MEMORY_BASIC_INFORMATION64 mbi = { 0 };
    ptr_t lastBase = 0;

    result.clear( );

    for (ptr_t memptr = minAddr(); memptr < maxAddr(); memptr = mbi.BaseAddress + mbi.RegionSize)
    {
        auto status = VirtualQueryExT( memptr, &mbi );

        if (status == STATUS_INVALID_PARAMETER)
            break;
        else if (status != STATUS_SUCCESS)
            continue;

        // Filter non-section regions
        if (mbi.State != MEM_COMMIT || mbi.Type != SEC_IMAGE || lastBase == mbi.AllocationBase)
            continue;

        uint8_t buf[0x1000] = { 0 };
        _UNICODE_STRING_T<DWORD64>* ustr = (decltype(ustr))(buf + 0x800);

        status = VirtualQueryExT( mbi.AllocationBase, MemorySectionName, ustr, sizeof(buf) / 2 );

        // Get additional 
        if (status == STATUS_SUCCESS)
        {
            ModuleData data;

            IMAGE_DOS_HEADER* phdrDos = reinterpret_cast<PIMAGE_DOS_HEADER>(buf);
            IMAGE_NT_HEADERS32 *phdrNt32 = nullptr;
            IMAGE_NT_HEADERS64 *phdrNt64 = nullptr;

            if (ReadProcessMemoryT( mbi.AllocationBase, buf, 0x800 ) != STATUS_SUCCESS)
                continue;

            phdrNt32 = reinterpret_cast<PIMAGE_NT_HEADERS32>(buf + phdrDos->e_lfanew);
            phdrNt64 = reinterpret_cast<PIMAGE_NT_HEADERS64>(phdrNt32);

            if (phdrDos->e_magic != IMAGE_DOS_SIGNATURE)
                continue;
            if (phdrNt32->Signature != IMAGE_NT_SIGNATURE)
                continue;

            if (phdrNt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            {
                data.size = phdrNt32->OptionalHeader.SizeOfImage;
                data.type = mt_mod32;
            }
            else if (phdrNt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                data.size = phdrNt64->OptionalHeader.SizeOfImage;
                data.type = mt_mod64;
            }

            // Hack for x86 OS
            if (_wowBarrier.x86OS == true)
            {
                _UNICODE_STRING_T<DWORD>* ustr32 = reinterpret_cast<_UNICODE_STRING_T<DWORD>*>(ustr);
                data.fullPath = Utils::ToLower( (const wchar_t*)ustr32->Buffer );
            }
            else
                data.fullPath = Utils::ToLower( (const wchar_t*)ustr->Buffer );

            data.name = Utils::StripPath( data.fullPath );
            data.baseAddress = mbi.AllocationBase;
            data.manual = false;

            result.emplace_back( data );
        }

        lastBase = mbi.AllocationBase;
    }

    return result.size();
}

/// <summary>
/// Enum pages containing valid PE headers
/// </summary>
/// <param name="result">Found modules</param>
/// <returns>Sections count</returns>
size_t Native::EnumPEHeaders( listModules& result )
{
    MEMORY_BASIC_INFORMATION64 mbi = { 0 };
    uint8_t buf[0x1000];
    ptr_t lastBase = 0;

    result.clear();

    for (ptr_t memptr = minAddr(); memptr < maxAddr(); memptr = mbi.BaseAddress + mbi.RegionSize)
    {
        auto status = VirtualQueryExT( memptr, &mbi );

        if (status == STATUS_INVALID_PARAMETER)
            break;
        else if (status != STATUS_SUCCESS)
            continue;

        // Filter regions
        if (mbi.State != MEM_COMMIT || 
             mbi.AllocationProtect == PAGE_NOACCESS || 
             mbi.AllocationProtect & PAGE_GUARD || 
             lastBase == mbi.AllocationBase)
        {
            continue;
        }

        ModuleData data;

        IMAGE_DOS_HEADER* phdrDos = reinterpret_cast<PIMAGE_DOS_HEADER>(buf);
        IMAGE_NT_HEADERS32 *phdrNt32 = nullptr;
        IMAGE_NT_HEADERS64 *phdrNt64 = nullptr;

        if (ReadProcessMemoryT( mbi.AllocationBase, buf, 0x1000 ) != STATUS_SUCCESS)
            continue;

        phdrNt32 = reinterpret_cast<PIMAGE_NT_HEADERS32>(buf + phdrDos->e_lfanew);
        phdrNt64 = reinterpret_cast<PIMAGE_NT_HEADERS64>(phdrNt32);

        if (phdrDos->e_magic != IMAGE_DOS_SIGNATURE)
            continue;
        if (phdrNt32->Signature != IMAGE_NT_SIGNATURE)
            continue;

        if (phdrNt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            data.size = phdrNt32->OptionalHeader.SizeOfImage;
            data.type = mt_mod32;
        }
        else if (phdrNt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            data.size = phdrNt64->OptionalHeader.SizeOfImage;
            data.type = mt_mod64;
        }

        data.baseAddress = mbi.AllocationBase;
        data.manual = false;

        // Try to get section name
        _UNICODE_STRING_T<DWORD64>* ustr = (decltype(ustr))buf;
        status = VirtualQueryExT( mbi.AllocationBase, MemorySectionName, ustr, sizeof(buf) );

        if (status == STATUS_SUCCESS)
        {
            // Hack for x86 OS
            if (_wowBarrier.x86OS == true)
            {
                _UNICODE_STRING_T<DWORD>* ustr32 = reinterpret_cast<_UNICODE_STRING_T<DWORD>*>(ustr);
                data.fullPath = Utils::ToLower( (const wchar_t*)ustr32->Buffer );
            }
            else
                data.fullPath = Utils::ToLower( (const wchar_t*)ustr->Buffer );

            data.name = Utils::StripPath( data.fullPath );
        }
        else
        {
            wchar_t buf[64];
            wsprintf( buf, L"Unknown_0x%I64x", data.baseAddress );

            data.fullPath = buf;
            data.name = data.fullPath;
        }

        result.emplace_back( data );

        lastBase = mbi.AllocationBase;
    }

    return result.size();
}

/// <summary>
/// Enumerate process modules
/// </summary>
/// <param name="result">Found modules</param>
/// <param name="mtype">Module type: x86 or x64</param>
/// <returns>Module count</returns>
size_t Native::EnumModules( listModules& result, eModSeachType search/*= LdrList*/, eModType mtype /*= mt_default */ )
{
    if (search == LdrList)
    {
        // Detect module type
        if (mtype == mt_default)
            mtype = _wowBarrier.targetWow64 ? mt_mod32 : mt_mod64;

        if (mtype == mt_mod32)
            return EnumModulesT<DWORD>( result );
        else
            return EnumModulesT<DWORD64>( result );
    }
    else if(search == Sections)
    {
        return EnumSections( result );
    }
    else if(search == PEHeaders)
    {
        return EnumPEHeaders( result );
    }

    return 0;
}

}