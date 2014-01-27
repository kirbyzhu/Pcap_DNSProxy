// This code is part of Pcap_DNSProxy
// Copyright (C) 2012-2014 Chengr28
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "Pcap_DNSProxy.h"

extern Configuration Parameter;

//Get local address(es)
bool __stdcall GetLocalAddress(sockaddr_storage &LocalAddr, const int Protocol)
{
//Initialization
	PSTR HostName = nullptr;
	try {
		HostName = new char[PACKET_MAXSIZE/8]();
	}
	catch (std::bad_alloc)
	{
		PrintError(System_Error, _T("Memory allocation failed"), NULL, NULL);

		TerminateService();
		return false;
	}
	addrinfo Hints = {0}, *Result = nullptr, *PTR = nullptr;
	memset(HostName, 0, PACKET_MAXSIZE/8);

	if (Protocol == AF_INET6) //IPv6
		Hints.ai_family = AF_INET6;
	else //IPv4
		Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_DGRAM;
	Hints.ai_protocol = IPPROTO_UDP;

//Get localhost name
	if (gethostname(HostName, PACKET_MAXSIZE/8) == SOCKET_ERROR)
	{
		PrintError(Winsock_Error, _T("Get localhost name failed"), WSAGetLastError(), NULL);

		delete[] HostName;
		return false;
	}

//Get localhost data
	SSIZE_T ResultGetaddrinfo = getaddrinfo(HostName, NULL, &Hints, &Result);
	if (ResultGetaddrinfo != 0)
	{
		PrintError(Winsock_Error, _T("Get local IP address failed"), ResultGetaddrinfo, NULL);

		freeaddrinfo(Result);
		delete[] HostName;
		return false;
	}
	delete[] HostName;

//Report
	for(PTR = Result;PTR != nullptr;PTR = PTR->ai_next)
	{
	//IPv6
		if (PTR->ai_family == AF_INET6 && Protocol == AF_INET6 && 
			!IN6_IS_ADDR_LINKLOCAL((in6_addr *)(PTR->ai_addr)) &&
			!(((PSOCKADDR_IN6)(PTR->ai_addr))->sin6_scope_id == 0)) //Get port from first(Main) IPv6 device
		{
			((PSOCKADDR_IN6)&LocalAddr)->sin6_addr = ((PSOCKADDR_IN6)(PTR->ai_addr))->sin6_addr;
			freeaddrinfo(Result);
			return true;
		}
	//IPv4
		else if (PTR->ai_family == AF_INET && Protocol == AF_INET && 
			((PSOCKADDR_IN)(PTR->ai_addr))->sin_addr.S_un.S_addr != INADDR_LOOPBACK && 
			((PSOCKADDR_IN)(PTR->ai_addr))->sin_addr.S_un.S_addr != INADDR_BROADCAST)
		{
			((PSOCKADDR_IN)&LocalAddr)->sin_addr = ((PSOCKADDR_IN)(PTR->ai_addr))->sin_addr;
			freeaddrinfo(Result);
			return true;
		}
	}

	freeaddrinfo(Result);
	return false;
}

//Get Ethernet Frame Check Sequence/FCS
ULONG __stdcall GetFCS(const PSTR Buffer, const size_t Length)
{
	ULONG Table[256] = {0}, Gx = 0x04C11DB7, Temp = 0, CRCTable = 0, Value = 0, UI = 0;
	char ReflectNum[] = {8, 32};
	int Index[3] = {0};

	for(Index[0] = 0;Index[0] <= 0xFF;Index[0]++)
    {
		Value = 0;
		UI = Index[0];
		for (Index[1] = 1;Index[1] < 9;Index[1]++)
		{
			if (UI & 1)
				Value |= 1 << (ReflectNum[0]-Index[1]);
			UI >>= 1;
		}
		Temp = Value;
		Table[Index[0]] = Temp << 24;

		for (Index[2] = 0;Index[2] < 8;Index[2]++)
		{
			unsigned long int t1 = 0, t2 = 0;
			unsigned long int Flag = Table[Index[0]] & 0x80000000;
			t1 = (Table[Index[0]] << 1);
			if (Flag == 0)
				t2 = 0;
			else
				t2 = Gx;
			Table[Index[0]] = t1 ^ t2;
        }
		CRCTable = Table[Index[0]];

		UI = Table[Index[0]];
		Value = 0;
		for (Index[1] = 1;Index[1] < 33;Index[1]++)
		{
			if (UI & 1)
				Value |= 1 << (ReflectNum[1] - Index[1]);
			UI >>= 1;
		}
		Table[Index[0]] = Value;
	}

	ULONG CRC = 0xFFFFFFFF;
	for (Index[0] = 0;Index[0] < (int)Length;Index[0]++)
		CRC = Table[(CRC ^ (*(Buffer + Index[0]))) & 0xFF]^(CRC >> 8);

	return ~CRC;
}

//Get Checksum
USHORT __stdcall GetChecksum(const USHORT *Buffer, size_t Length)
{
	ULONG Checksum = 0;
	while(Length > 1)
	{ 
		Checksum += *Buffer++;
		Length -= sizeof(USHORT);
	}
	
	if (Length)
		Checksum += *(PUCHAR)Buffer;

	Checksum = (Checksum >> 16) + (Checksum & 0xFFFF);
	Checksum += (Checksum >> 16);

	return (USHORT)(~Checksum);
}

//Get ICMPv6 checksum
USHORT __stdcall ICMPv6Checksum(const PSTR Buffer, const size_t Length)
{
//Initialization
	PSTR Validation = nullptr;
	try {
		Validation = new char[PACKET_MAXSIZE]();
	}
	catch (std::bad_alloc)
	{
		PrintError(System_Error, _T("Memory allocation failed"), NULL, NULL);

		TerminateService();
		return 0;
	}
	memset(Validation, 0, PACKET_MAXSIZE);
	USHORT Result = 0;

//Get checksum
	if (Length - sizeof(ipv6_hdr) > 0)
	{
		ipv6_psd_hdr *psd = (ipv6_psd_hdr *)Validation;
		psd->Dst = ((ipv6_hdr *)Buffer)->Dst;
		psd->Src = ((ipv6_hdr *)Buffer)->Src;
		psd->Length = htonl((ULONG)(Length - sizeof(ipv6_hdr)));
		psd->Next_Header = IPPROTO_ICMPV6;

		memcpy(Validation + sizeof(ipv6_psd_hdr), Buffer + sizeof(ipv6_hdr), Length - sizeof(ipv6_hdr));
		Result = GetChecksum((PUSHORT)Validation, sizeof(ipv6_psd_hdr) + Length - sizeof(ipv6_hdr));
	}

	delete[] Validation;
	return Result;
}

//Get UDP checksum
USHORT __stdcall UDPChecksum(const PSTR Buffer, const size_t Length, const size_t Protocol)
{
//Initialization
	PSTR Validation = nullptr;
	try {
		Validation = new char[PACKET_MAXSIZE]();
	}
	catch (std::bad_alloc)
	{
		PrintError(System_Error, _T("Memory allocation failed"), NULL, NULL);

		TerminateService();
		return 0;
	}
	memset(Validation, 0, PACKET_MAXSIZE);
	
//Get checksum
	USHORT Result = 0;
	if (Protocol == AF_INET6 && Length - sizeof(ipv6_hdr) > 0) //IPv6
	{
		ipv6_psd_hdr *psd = (ipv6_psd_hdr *)Validation;
		psd->Dst = ((ipv6_hdr *)Buffer)->Dst;
		psd->Src = ((ipv6_hdr *)Buffer)->Src;
		psd->Length = htonl((ULONG)(Length - sizeof(ipv6_hdr)));
		psd->Next_Header = IPPROTO_UDP;

		memcpy(Validation + sizeof(ipv6_psd_hdr), Buffer + sizeof(ipv6_hdr), Length - sizeof(ipv6_hdr));
		Result = GetChecksum((PUSHORT)Validation, sizeof(ipv6_psd_hdr) + Length - sizeof(ipv6_hdr));
	}
	else if (Protocol == AF_INET && Length - sizeof(ipv4_hdr) > 0) //IPv4
	{
		ipv4_psd_hdr *psd = (ipv4_psd_hdr *)Validation;
		psd->Dst = ((ipv4_hdr *)Buffer)->Dst;
		psd->Src = ((ipv4_hdr *)Buffer)->Src;
		psd->Length = htons((USHORT)(Length - sizeof(ipv4_hdr)));
		psd->Protocol = IPPROTO_UDP;

		memcpy(Validation+sizeof(ipv4_psd_hdr), Buffer + sizeof(ipv4_hdr), Length - sizeof(ipv4_hdr));
		Result = GetChecksum((PUSHORT)Validation, sizeof(ipv4_psd_hdr) + Length - sizeof(ipv4_hdr));
	}

	delete[] Validation;
	return Result;
}

//Convert data from unsigned char/UCHAR to DNS query
size_t __stdcall CharToDNSQuery(const PSTR FName, PSTR TName)
{
	int Index[] = {(int)strlen(FName) - 1, 0, 0};
	Index[2] = Index[0] + 1;
	TName[Index[0] + 2] = 0;

	for (;Index[0] >= 0;Index[0]--,Index[2]--)
	{
		if (FName[Index[0]] == 46)
		{
			TName[Index[2]] = Index[1];
			Index[1] = 0;
		}
		else
		{
			TName[Index[2]] = FName[Index[0]];
			Index[1]++;
		}
	}
	TName[Index[2]] = Index[1];

	return strlen(TName) + 1;
}

//Convert data from DNS query to unsigned char/UCHAR
size_t __stdcall DNSQueryToChar(const PSTR TName, PSTR FName)
{
	size_t uIndex = 0;
	int Index[] = {0, 0};

	for(uIndex = 0;uIndex < PACKET_MAXSIZE/8;uIndex++)
	{
		if (uIndex == 0)
		{
			Index[0] = TName[uIndex];
		}
		else if (uIndex == Index[0] + Index[1] + 1)
		{
			Index[0] = TName[uIndex];
			if (Index[0] == 0)
				break;
			Index[1] = (int)uIndex;

			FName[uIndex - 1] = 46;
		}
		else {
			FName[uIndex - 1] = TName[uIndex];
		}
	}

	return uIndex;
}

//Convert local address(es) to reply DNS PTR Record(s)
size_t __stdcall LocalAddressToPTR(std::string &Result, const size_t Protocol)
{
//Initialization
	PSTR Addr = nullptr;
	try {
		Addr = new char[PACKET_MAXSIZE/8]();
	}
	catch(std::bad_alloc)
	{
		PrintError(System_Error, _T("Memory allocation failed"), NULL, NULL);

		TerminateService();
		return EXIT_FAILURE;
	}
	memset(Addr, 0, PACKET_MAXSIZE/8);
	sockaddr_storage LocalAddr = {0};

	if (!GetLocalAddress(LocalAddr, (int)Protocol))
	{
		delete[] Addr;
		return EXIT_FAILURE;
	}

	SSIZE_T Index = 0;
	size_t Location = 0, Colon = 0;
	while (true)
	{
	//IPv6
		if (Protocol == AF_INET6)
		{
			std::string Temp[2];
			Location = 0;
			Colon = 0;

		//Convert from in6_addr to string
			if (inet_ntop(AF_INET6, &((PSOCKADDR_IN6)&LocalAddr)->sin6_addr, Addr, PACKET_MAXSIZE/8) == nullptr)
			{
				PrintError(Winsock_Error, _T("Local IPv6 Address format error"), WSAGetLastError(), NULL);

				delete[] Addr;
				return EXIT_FAILURE;
			}
			Temp[0] = Addr;

		//Convert to standard IPv6 address format A part(":0:" -> ":0000:")
			while (Temp[0].find(":0:", Index) != std::string::npos)
			{
				Index = Temp[0].find(":0:", Index);
				Temp[0].replace(Index, 3, ":0000:");
			}

		//Count colon
			for (Index = 0;(size_t)Index < Temp[0].length();Index++)
			{
				if (Temp[0].at(Index) == 58)
					Colon++;
			}

		//Convert to standard IPv6 address format B part("::" -> ":0000:...")
			Location = Temp[0].find("::");
			Colon = 8 - Colon;
			Temp[1].append(Temp[0], 0, Location);
			while (Colon != 0)
			{
				Temp[1].append(":0000");
				Colon--;
			}
			Temp[1].append(Temp[0], Location + 1, Temp[0].length() - Location + 1);

			for (std::string::iterator iter = Temp[1].begin();iter != Temp[1].end();iter++)
			{
				if (*iter == 58)
					Temp[1].erase(iter);
			}

		//Convert to DNS PTR Record and copy to Result
			for (Index = Temp[1].length() - 1;Index != -1;Index--)
			{
				char Word[] = {0, 0};
				Word[0] = Temp[1].at(Index);
				Result.append(Word);
				Result.append(".");
			}

			Result.append("ip6.arpa");
		}
	//IPv4
		else {
			char CharAddr[4][4] = {0};
			size_t Localtion[] = {0, 0};

		//Convert from in_addr to string
			if (inet_ntop(AF_INET, &((PSOCKADDR_IN)&LocalAddr)->sin_addr, Addr, PACKET_MAXSIZE/8) == nullptr)
			{
				PrintError(Winsock_Error, _T("Local IPv4 Address format error"), WSAGetLastError(), NULL);

				delete[] Addr;
				return EXIT_FAILURE;
			}

		//Detach Address data
			for (Index = 0;(size_t)Index < strlen(Addr);Index++)
			{
				if (Addr[Index] == 46)
				{
					Localtion[1] = 0;
					Localtion[0]++;
				}
				else {
					CharAddr[Localtion[0]][Localtion[1]] = Addr[Index];
					Localtion[1]++;
				}
			}

		//Convert to DNS PTR Record and copy to Result
			for (Index = 4;Index > 0;Index--)
			{
				Result.append(CharAddr[Index - 1]);
				Result.append(".");
			}

			Result.append("in-addr.arpa");
		}

	//Auto-refresh
		if (Parameter.Hosts == 0)
		{
			delete[] Addr;
			return EXIT_SUCCESS;
		}
		else {
			Sleep((DWORD)Parameter.Hosts);
		}
	}

	delete[] Addr;
	return EXIT_SUCCESS;
}

//Make ramdom domains
void __stdcall RamdomDomain(PSTR Domain, const size_t Length)
{
	static const char DomainTable[] = (".-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"); //Preferred name syntax(Section 2.3.1 in RFC 1035)
	memset(Parameter.DomainTestOptions.DomainTest, 0, PACKET_MAXSIZE/8);
	size_t RamdomLength = 0, Sign = 0;

//Make ramdom numbers
//Formula: [M, N] -> rand()%(N-M+1)+M
	srand((UINT)time((time_t *)NULL));
	RamdomLength = rand() % 61 + 3; //Domain length is between 3 and 63(Labels must be 63 characters/bytes or less, Section 2.3.1 in RFC 1035)
	for (Sign = 0;Sign < RamdomLength;Sign++)
		Domain[Sign] = DomainTable[rand() % (int)(sizeof(DomainTable) - 2)];

	return;
}