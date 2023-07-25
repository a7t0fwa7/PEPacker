#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <cryptopp/filters.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/gzip.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>
#include <cryptopp/files.h>


#pragma warning(disable: 6011)

#define SECTION_NAME "UPX"


#pragma optimize("", off)
byte unused_global() // ��δ�����ԳŴ�.text��
{
#pragma section(".text")
	static __declspec(allocate(".text")) byte Sponge[0x100000];
	return Sponge[sizeof(Sponge) - 1];
}
#pragma optimize("", on)


typedef struct _PeConfig {

	PVOID					PeAddress;
	SIZE_T					PeSize;

	PIMAGE_DOS_HEADER		DosHeader;
	PIMAGE_NT_HEADERS		NtHeaders;

	PIMAGE_DATA_DIRECTORY	ImportTable;		//IMAGE_DIRECTORY_ENTRY_IMPORT
	PIMAGE_DATA_DIRECTORY	TlsTable;			//IMAGE_DIRECTORY_ENTRY_TLS
	PIMAGE_DATA_DIRECTORY	RelocationTable;	//IMAGE_DIRECTORY_ENTRY_BASERELOC
	PIMAGE_DATA_DIRECTORY	ExceptionTable;		//IMAGE_DIRECTORY_ENTRY_EXCEPTION

	PIMAGE_SECTION_HEADER	SectionHeaders;

} PeConfig, * PPeConfig;


BYTE* FindPackerSection(DWORD* SectionSize); // ���ﷵ�ص��� VirtualSize
BYTE* DecryptData(BYTE* Data, INT Length, INT* OutputLength);

BOOL InitPeConfig(PPeConfig Pe, PVOID PeAddress, SIZE_T PeSize);
BOOL FixImportAddressTable(PeConfig Pe, PVOID Address);
BOOL Relocate(PeConfig Pe, PVOID Address);
PVOID UnpackPE(PeConfig Pe, PVOID PeAddress, PVOID Address);


int _tmain(int argc, TCHAR* argv[])
{
	// �Ŵ�.text��
	unused_global();

	//
	// Ѱ�ҼӿǶ�
	//

	DWORD SectionSize = 0;
	BYTE* SectionData = FindPackerSection(&SectionSize);

	if (SectionData == NULL || SectionSize == 0) {
#ifdef _DEBUG
		_tprintf(_T("[x] Could Not Find %hs Section !\n"), SECTION_NAME);
#endif
		return -1;
	}

#ifdef _DEBUG
	_tprintf(_T("[!] SectionData: %p, SectionSize: %#x\n"), SectionData, SectionSize);
#endif

	//
	// ���ܼӿǶ�
	//

	INT DecryptedLength = 0;
	BYTE* DecryptedData = DecryptData(SectionData, SectionSize, &DecryptedLength);

	if (DecryptedData == NULL) {
		return -1;
	}

	//
	// �����ڴ�׼��չ��PE
	//

	PVOID PeAddress = DecryptedData;
	DWORD PeSize = DecryptedLength;

	PeConfig Pe = { 0 };
	if (!InitPeConfig(&Pe, PeAddress, PeSize)) {
#ifdef _DEBUG
		_tprintf(_T("[x] InitPeConfig Failed\n"));
#endif
		return -1;
	}

	PVOID Address = VirtualAlloc((PVOID)Pe.NtHeaders->OptionalHeader.ImageBase,
		Pe.NtHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

	if (Address == NULL) {
#ifdef _DEBUG
		_tprintf(_T("[x] First VirtualAlloc Failed. Error: %#x\n"), GetLastError());
		_tprintf(_T("[!] You have second chance. Goodluck.\n"));
#endif
		Address = VirtualAlloc(NULL, Pe.NtHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (Address == NULL) {
#ifdef _DEBUG
			_tprintf(_T("[x] Second VirtualAlloc Failed. Error: %#x\n"), GetLastError());
#endif
			return -1;
		}
	}

#ifdef _DEBUG
	_tprintf(_T("[+] Preferable Address: 0x%p \n"), (PVOID)Pe.NtHeaders->OptionalHeader.ImageBase);
	_tprintf(_T("[+] Allocated Address: 0x%p \n"), Address);
	_tprintf(_T("[+] Allocation Size: %#x \n"), Pe.NtHeaders->OptionalHeader.SizeOfImage);
#endif

	//
	// �ڴ�չ��PE
	//

	PVOID EP = UnpackPE(Pe, PeAddress, Address);
	if (EP == NULL) {
		return -1;
	}

	// Ĩ��PEͷ
	ZeroMemory(Address, (SIZE_T)Pe.SectionHeaders[0].VirtualAddress);

	// �ͷ��ڴ�
	free(PeAddress);

	//
	// ����PE
	//

#ifdef _DEBUG
	_tprintf(_T("[+] Running The Packed Pe's Entry Point ... \n\n\n"));
#endif

	// �������߳�ִ��PE
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)EP, 0, 0, NULL);
	if (hThread == NULL) {
#ifdef _DEBUG
		_tprintf(_T("[x] CreateThread Failed. Error: %#x\n"), GetLastError());
#endif
		return -1;
	}

	WaitForSingleObject(hThread, INFINITE);

	return 0;
}


BYTE* FindPackerSection(DWORD* SectionSize)
{
	PVOID Stub = GetModuleHandle(NULL);
	PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)Stub;
	PIMAGE_NT_HEADERS NtHeaders = (PIMAGE_NT_HEADERS)((ULONG_PTR)Stub + DosHeader->e_lfanew);
	PIMAGE_SECTION_HEADER SectionHeaders = (PIMAGE_SECTION_HEADER)((ULONG_PTR)NtHeaders + sizeof(IMAGE_NT_HEADERS));


	for (int i = 0; i <= NtHeaders->FileHeader.NumberOfSections; i++) {
		if (strncmp((CHAR*)SectionHeaders[i].Name, SECTION_NAME, strlen(SECTION_NAME)) == 0) {
			*SectionSize = SectionHeaders[i].Misc.VirtualSize;
			return (BYTE*)((ULONG_PTR)Stub + SectionHeaders[i].VirtualAddress);
		}
	}

	return NULL;
}


BYTE* DecryptData(BYTE* Data, INT Length, INT* OutputLength)
{
	std::string Key;
	std::string IV = "0123456789ABCDEF";

	try {
		CryptoPP::FileSource fs("LICENSE.txt", true,
			new CryptoPP::HexDecoder(
				new CryptoPP::StringSink(Key)));
	}
	catch (CryptoPP::Exception& e) {
#ifdef _DEBUG
		_tprintf(_T("[x] Read Key Error: %hs\n"), e.what());
#endif
		return NULL;
	}

	if (Key.length() != 16 && Key.length() != 24 && Key.length() != 32) {
#ifdef _DEBUG
		_tprintf(_T("[x] Wrong Key Length\n"));
#endif
		return NULL;
	}

	std::string Input((CHAR*)Data, Length);
	CryptoPP::StringSource Source(Input, false);

	std::string Output;
	CryptoPP::StringSink Sink(Output);

	try {
		CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption Decryption;
		Decryption.SetKeyWithIV((byte*)Key.c_str(), Key.length(), (byte*)IV.c_str());
		CryptoPP::StreamTransformationFilter Decryptor(Decryption);

		CryptoPP::Gunzip Gunzip;
		CryptoPP::Base64Decoder Base64Decoder;

		Source.Attach(new CryptoPP::Redirector(Base64Decoder));
		Base64Decoder.Attach(new CryptoPP::Redirector(Decryptor));
		Decryptor.Attach(new CryptoPP::Redirector(Gunzip));
		Gunzip.Attach(new CryptoPP::Redirector(Sink));

		Source.PumpAll();
	}
	catch (CryptoPP::Exception& e) {
#ifdef _DEBUG
		_tprintf(_T("[x] CryptoPP Decrypt Error: %hs\n"), e.what());
#endif
		return NULL;
	}

	INT OL = (INT)Output.length();
	BYTE* DecryptedData = (BYTE*)malloc(OL);
	if (DecryptedData == NULL) {
#ifdef _DEBUG
		_tprintf(_T("[x] Malloc Failed, Size: %#x. Error: %#x\n"), OL, GetLastError());
#endif
		return NULL;
	}
	memcpy(DecryptedData, Output.c_str(), OL);

	*OutputLength = OL;

	return DecryptedData;
}


BOOL InitPeConfig(PPeConfig Pe, PVOID PeAddress, SIZE_T PeSize)
{
	if (PeAddress == NULL || PeSize == NULL) {
		return FALSE;
	}

	Pe->PeAddress = PeAddress;
	Pe->PeSize = PeSize;

	Pe->DosHeader = (PIMAGE_DOS_HEADER)PeAddress;
	if (Pe->DosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
		return FALSE;
	}

	Pe->NtHeaders = (PIMAGE_NT_HEADERS)((PBYTE)PeAddress + Pe->DosHeader->e_lfanew);
	if (Pe->NtHeaders->Signature != IMAGE_NT_SIGNATURE) {
		return FALSE;
	}

	Pe->ImportTable = &Pe->NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	Pe->TlsTable = &Pe->NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
	Pe->RelocationTable = &Pe->NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
	Pe->ExceptionTable = &Pe->NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
	Pe->SectionHeaders = (PIMAGE_SECTION_HEADER)((SIZE_T)Pe->NtHeaders + sizeof(IMAGE_NT_HEADERS));

	if (Pe->DosHeader == NULL || Pe->NtHeaders == NULL ||
		Pe->ImportTable == NULL || Pe->TlsTable == NULL ||
		Pe->RelocationTable == NULL || Pe->ExceptionTable == NULL ||
		Pe->SectionHeaders == NULL) {
		return FALSE;
	}

	return TRUE;
}


BOOL FixImportAddressTable(PeConfig Pe, PVOID Address)
{
	// ����DLL
	for (DWORD i = 0; i < Pe.ImportTable->Size; i += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {

		PIMAGE_IMPORT_DESCRIPTOR ImageImportDescriptor = (IMAGE_IMPORT_DESCRIPTOR*)((ULONG_PTR)Address + Pe.ImportTable->VirtualAddress + i);

		if (ImageImportDescriptor->OriginalFirstThunk == NULL && ImageImportDescriptor->FirstThunk == NULL) {
			break;
		}

		LPSTR		DllName = (LPSTR)((ULONG_PTR)Address + ImageImportDescriptor->Name);
		ULONG_PTR	Head = ImageImportDescriptor->FirstThunk;
		ULONG_PTR	Next = ImageImportDescriptor->OriginalFirstThunk;
		SIZE_T		HeadSize = 0;
		SIZE_T		NextSize = 0;
		HMODULE		hDLL = LoadLibraryA(DllName);

		if (hDLL == NULL) {
			return FALSE;
		}

		if (Next == NULL) {
			Next = ImageImportDescriptor->FirstThunk;
		}

		// ��ʼ���뺯��
		while (TRUE) {

			PIMAGE_THUNK_DATA			FirstThunk = (IMAGE_THUNK_DATA*)((ULONG_PTR)Address + HeadSize + Head);
			PIMAGE_THUNK_DATA			OrginFirstThunk = (IMAGE_THUNK_DATA*)((ULONG_PTR)Address + NextSize + Next);
			PIMAGE_IMPORT_BY_NAME		FunctionName = NULL;
			ULONG_PTR					Function = NULL;

			if (FirstThunk->u1.Function == NULL) {
				break;
			}

			// ͨ��DLL��ŵ���
			if (OrginFirstThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
				Function = (ULONG_PTR)GetProcAddress(hDLL, (CHAR*)(OrginFirstThunk->u1.Ordinal & 0xFFFF));
			}

			// ͨ���������ֵ���
			else {
				FunctionName = (PIMAGE_IMPORT_BY_NAME)((SIZE_T)Address + OrginFirstThunk->u1.AddressOfData);
				Function = (ULONG_PTR)GetProcAddress(hDLL, FunctionName->Name);
			}

			if (Function == NULL) {
#ifdef _DEBUG
				_tprintf(_T("[x] Could Not Import !%hs.%hs\n"), DllName, FunctionName->Name);
#endif
				return FALSE;
			}

			FirstThunk->u1.Function = Function;

			// ��һ������
			HeadSize += sizeof(IMAGE_THUNK_DATA);
			NextSize += sizeof(IMAGE_THUNK_DATA);

		}
	}

	return TRUE;
}


BOOL Relocate(PeConfig Pe, PVOID Address)
{
	typedef struct _BASE_RELOCATION_ENTRY {
		WORD Offset : 12;
		WORD Type : 4;
	} BASE_RELOCATION_ENTRY, * PBASE_RELOCATION_ENTRY;

	PIMAGE_BASE_RELOCATION  RelocationTable = (PIMAGE_BASE_RELOCATION)((ULONG_PTR)Address + Pe.RelocationTable->VirtualAddress);
	ULONG_PTR				Offset = (ULONG_PTR)Address - Pe.NtHeaders->OptionalHeader.ImageBase;

	while (RelocationTable->VirtualAddress != 0) {

		// �ض�λ�����IMAGE_BASE_RELOCATION�ṹ��������һ���
		PBASE_RELOCATION_ENTRY Relocation = (PBASE_RELOCATION_ENTRY)((ULONG_PTR)RelocationTable + sizeof(IMAGE_BASE_RELOCATION));

		while ((PBYTE)Relocation != (PBYTE)RelocationTable + RelocationTable->SizeOfBlock) {

			switch (Relocation->Type) {
			case IMAGE_REL_BASED_DIR64:
				*((ULONG_PTR*)((ULONG_PTR)Address + RelocationTable->VirtualAddress + Relocation->Offset)) += Offset;
				break;
			case IMAGE_REL_BASED_HIGHLOW:
				*((DWORD*)((ULONG_PTR)Address + RelocationTable->VirtualAddress + Relocation->Offset)) += (DWORD)Offset;
				break;

			case IMAGE_REL_BASED_HIGH:
				*((WORD*)((ULONG_PTR)Address + RelocationTable->VirtualAddress + Relocation->Offset)) += HIWORD(Offset);
				break;

			case IMAGE_REL_BASED_LOW:
				*((WORD*)((ULONG_PTR)Address + RelocationTable->VirtualAddress + Relocation->Offset)) += LOWORD(Offset);
				break;

			case IMAGE_REL_BASED_ABSOLUTE:
				break;

			default:
#ifdef _DEBUG
				_tprintf(_T("[x] Unknown Relocation Type: %#x\n"), Relocation->Type);
#endif
				return FALSE;
			}

			Relocation++;
		}

		RelocationTable = (PIMAGE_BASE_RELOCATION)Relocation;
	}

	return TRUE;
}


PVOID UnpackPE(PeConfig Pe, PVOID PeAddress, PVOID Address)
{
	// ����PEͷ
	memcpy(Address, PeAddress, Pe.NtHeaders->OptionalHeader.SizeOfHeaders);

	// ���Ƹ���
	for (int i = 0; i < Pe.NtHeaders->FileHeader.NumberOfSections; i++) {
#ifdef _DEBUG
		_tprintf(_T("\t[%0.2d] Section: %hs. Copying 0x%p To 0x%p of Size: %d\n"), i,
			Pe.SectionHeaders[i].Name,
			(PVOID)((ULONG_PTR)PeAddress + Pe.SectionHeaders[i].PointerToRawData),
			(PVOID)((ULONG_PTR)Address + Pe.SectionHeaders[i].VirtualAddress),
			Pe.SectionHeaders[i].SizeOfRawData);
#endif

		memcpy(
			(BYTE*)Address + Pe.SectionHeaders[i].VirtualAddress,
			(BYTE*)PeAddress + Pe.SectionHeaders[i].PointerToRawData,
			Pe.SectionHeaders[i].SizeOfRawData);
	}

	// �޸�IAT
	if (!FixImportAddressTable(Pe, Address)) {
#ifdef _DEBUG
		_tprintf(_T("[x] Failed To Fix The IAT.\n"));
#endif
		return NULL;
	}

	// �ض�λ
	if (Address != (PVOID)Pe.NtHeaders->OptionalHeader.ImageBase) {
#ifdef _DEBUG
		_tprintf(_T("[!] The Allocated Mem Is Different Than The Preferable Address, Handling Reallocations ... \n"));
#endif

		if (Pe.RelocationTable->VirtualAddress == NULL) {
#ifdef _DEBUG
			_tprintf(_T("[x] Image base has changed and relocation directory not exist\n"));
#endif
			return NULL;
		}

		if (!Relocate(Pe, Address)) {
#ifdef _DEBUG
			_tprintf(_T("[!] Failed To Fix The Re-Allocation\n"));
#endif
			return NULL;
		}
	}

	// ע���쳣����
	if (Pe.ExceptionTable->Size) {
#ifdef _DEBUG
		_tprintf(_T("[!] Handling The Packed Pe's Exception Handlers ... \n"));
#endif
		PIMAGE_RUNTIME_FUNCTION_ENTRY ImgRunFuncEntry = (PIMAGE_RUNTIME_FUNCTION_ENTRY)((ULONG_PTR)Address + Pe.ExceptionTable->VirtualAddress);
		if (!RtlAddFunctionTable(ImgRunFuncEntry, (Pe.ExceptionTable->Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY)) - 1, (DWORD64)Address)) {
#ifdef _DEBUG
			_tprintf(_T("[!] RtlAddFunctionTable Failed. Error: %#x\n"), GetLastError());
#endif
			return NULL;
		}
	}

	// �޸���Ȩ�ޣ���Ҫ��TLS�ص�����֮ǰ
	for (DWORD i = 0; i < Pe.NtHeaders->FileHeader.NumberOfSections; i++) {

		DWORD Protection = 0;

		if (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_WRITE)
			Protection = PAGE_WRITECOPY;

		if (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_READ)
			Protection = PAGE_READONLY;

		if ((Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_WRITE) && (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_READ))
			Protection = PAGE_READWRITE;

		if (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
			Protection = PAGE_EXECUTE;

		if ((Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) && (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_WRITE))
			Protection = PAGE_EXECUTE_WRITECOPY;

		if ((Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) && (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_READ))
			Protection = PAGE_EXECUTE_READ;

		if ((Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) && (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_WRITE) && (Pe.SectionHeaders[i].Characteristics & IMAGE_SCN_MEM_READ))
			Protection = PAGE_EXECUTE_READWRITE;

#ifdef _DEBUG
		_tprintf(_T("\t[%0.2d] Setting Mem Permissions To %#04x on 0x%p\n"), i, Protection, (PVOID)((ULONG_PTR)Address + Pe.SectionHeaders[i].VirtualAddress));
#endif

		DWORD OldProtect;
		VirtualProtect((BYTE*)Address + Pe.SectionHeaders[i].VirtualAddress, Pe.SectionHeaders[i].SizeOfRawData, Protection, &OldProtect);
	}

	// TLS�ص�����
	if (Pe.TlsTable->Size) {
#ifdef _DEBUG
		_tprintf(_T("[!] Found Tls Callbacks, Setting Up For Execution ... \n"));
#endif

		PIMAGE_TLS_DIRECTORY pImgTlsDir = (PIMAGE_TLS_DIRECTORY)((ULONG_PTR)Address + Pe.TlsTable->VirtualAddress);
		PIMAGE_TLS_CALLBACK* ppCallback = (PIMAGE_TLS_CALLBACK*)(pImgTlsDir->AddressOfCallBacks);
		for (; *ppCallback; ppCallback++) {
			(*ppCallback)((LPVOID)Address, DLL_PROCESS_ATTACH, NULL);
		}
	}

	return (PVOID)((ULONG_PTR)Address + Pe.NtHeaders->OptionalHeader.AddressOfEntryPoint);
}