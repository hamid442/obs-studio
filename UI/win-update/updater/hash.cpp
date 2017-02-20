#include "updater.hpp"

#include <util/windows/WinHandle.hpp>

void HashToString(const BYTE *in, wchar_t *out)
{
	const char alphabet[] = "0123456789abcdef";

	for (int i = 0; i != 20; i++) {
		out[2 * i]     = alphabet[in[i] / 16];
		out[2 * i + 1] = alphabet[in[i] % 16];
	}

	out[40] = 0;
}

void StringToHash(const wchar_t *in, BYTE *out)
{
	int temp;

	for (int i = 0; i < 20; i++) {
		swscanf_s(in + i * 2, L"%02x", &temp);
		out[i] = (BYTE)temp;
	}
}

bool CalculateFileHash(const wchar_t *path, BYTE *hash)
{
	BYTE      buf[65536];
	CryptHash hHash;
	WinHandle hFile;

	if (!CryptCreateHash(hProvider, CALG_SHA1, 0, 0, &hHash))
		return false;

	hFile = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			/* A missing file is OK */
			memset(hash, 0, 20);
			return true;
		}

		return false;
	}

	for (;;) {
		DWORD read;

		if (!ReadFile(hFile, buf, sizeof(buf), &read, NULL))
			return false;
		if (!read)
			break;

		if (!CryptHashData(hHash, buf, read, 0))
			return false;
	}

	DWORD hashLength = 20;
	if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLength, 0))
		return false;

	return true;
}
