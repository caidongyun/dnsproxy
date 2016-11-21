﻿// This code is part of Pcap_DNSProxy
// A local DNS server based on WinPcap and LibPcap
// Copyright (C) 2012-2016 Chengr28
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


#include "DNSCurve.h"

/* DNSCurve/DNSCrypt Protocol version 2

Client -> Server:
*  8 bytes: Magic query bytes
* 32 bytes: The client's DNSCurve public key (crypto_box_PUBLICKEYBYTES)
* 12 bytes: A client-selected nonce for this packet (crypto_box_NONCEBYTES / 2)
* 16 bytes: Poly1305 MAC (crypto_box_ZEROBYTES - crypto_box_BOXZEROBYTES)
* Variable encryption data ...

Server -> Client:
*  8 bytes: The string r6fnvWJ8 (DNSCRYPT_MAGIC_RESPONSE)
* 12 bytes: The client's nonce (crypto_box_NONCEBYTES / 2)
* 12 bytes: A server-selected nonce extension (crypto_box_NONCEBYTES / 2)
* 16 bytes: Poly1305 MAC (crypto_box_ZEROBYTES - crypto_box_BOXZEROBYTES)
* Variable encryption data ...

Using TCP protocol:
* 2 bytes: DNSCurve/DNSCrypt data payload length
* Variable DNSCurve/DNSCrypt data ...

*/

#if defined(ENABLE_LIBSODIUM)
//DNSCurve check padding data length
ssize_t DNSCurvePaddingData(
	const bool SetPadding, 
	uint8_t * const Buffer, 
	const ssize_t Length)
{
//Set padding data sign.
	if (SetPadding)
	{
		Buffer[Length] = (uint8_t)DNSCRYPT_PADDING_SIGN_STRING;
	}
//Check padding data sign.
	else if (Length > (ssize_t)DNS_PACKET_MINSIZE)
	{
		ssize_t Index = 0;

	//Check padding data sign(0x80).
		for (Index = Length - 1U;Index > (ssize_t)DNS_PACKET_MINSIZE;--Index)
		{
			if (Buffer[Index] == DNSCRYPT_PADDING_SIGN)
				return Index;
		}

	//Check no null sign.
		for (Index = Length - 1U;Index > (ssize_t)DNS_PACKET_MINSIZE;--Index)
		{
			if (Buffer[Index] > 0)
				return Index;
		}
	}

	return 0;
}

//DNSCurve verify keypair
bool DNSCurveVerifyKeypair(
	const uint8_t * const PublicKey, 
	const uint8_t * const SecretKey)
{
//Initialization
	uint8_t Test_PublicKey[crypto_box_PUBLICKEYBYTES]{0}, Validation[crypto_box_PUBLICKEYBYTES + crypto_box_SECRETKEYBYTES + crypto_box_ZEROBYTES]{0};
	DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> Test_SecretKey(crypto_box_PUBLICKEYBYTES);

//Keypair, Nonce and validation data
	if (crypto_box_keypair(
			Test_PublicKey, 
			Test_SecretKey.Buffer) == 0)
				memcpy_s(Validation + crypto_box_ZEROBYTES, crypto_box_PUBLICKEYBYTES + crypto_box_SECRETKEYBYTES, PublicKey, crypto_box_PUBLICKEYBYTES);
	else 
		return false;

//Make DNSCurve Test Nonce, 0x00 - 0x23(ASCII).
	uint8_t Nonce[crypto_box_NONCEBYTES]{0};
	for (size_t Index = 0;Index < crypto_box_NONCEBYTES;++Index)
		*(Nonce + Index) = (uint8_t)Index;

//Verify keys
	if (crypto_box(
			Validation, 
			Validation, 
			crypto_box_PUBLICKEYBYTES + crypto_box_ZEROBYTES, 
			Nonce, 
			Test_PublicKey, 
			SecretKey) != 0 || 
		crypto_box_open(
			Validation, 
			Validation, 
			crypto_box_PUBLICKEYBYTES + crypto_box_ZEROBYTES, 
			Nonce, 
			PublicKey, 
			Test_SecretKey.Buffer) != 0)
				return false;

	return true;
}

//DNSCurve select socket data of DNS target(Multiple threading)
bool DNSCurveSelectTargetSocket(
	const uint16_t Protocol, 
	bool &IsIPv6, 
	bool ** const IsAlternate)
{
	IsIPv6 = false;

//IPv6
	if (DNSCurveParameter.DNSCurve_Target_Server_Main_IPv6.AddressData.Storage.ss_family > 0 && 
		((DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_BOTH && GlobalRunningStatus.GatewayAvailable_IPv6) || //Auto select
		DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 || //IPv6
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 && DNSCurveParameter.DNSCurve_Target_Server_Main_IPv4.AddressData.Storage.ss_family == 0))) //Non-IPv4
	{
		IsIPv6 = true;
		if (Protocol == IPPROTO_TCP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_TCP_IPV6];
		else if (Protocol == IPPROTO_UDP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_UDP_IPV6];
		else 
			return false;
	}
//IPv4
	else if (DNSCurveParameter.DNSCurve_Target_Server_Main_IPv4.AddressData.Storage.ss_family > 0 && 
		((DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_BOTH && GlobalRunningStatus.GatewayAvailable_IPv4) || //Auto select
		DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV4 || //IPv4
		(DNSCurveParameter.DNSCurveProtocol_Network == REQUEST_MODE_IPV6 && DNSCurveParameter.DNSCurve_Target_Server_Main_IPv6.AddressData.Storage.ss_family == 0))) //Non-IPv6
	{
		IsIPv6 = false;
		if (Protocol == IPPROTO_TCP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_TCP_IPV4];
		else if (Protocol == IPPROTO_UDP)
			*IsAlternate = &AlternateSwapList.IsSwap[ALTERNATE_TYPE_DNSCURVE_UDP_IPV4];
		else 
			return false;
	}
	else {
		return false;
	}

	return true;
}

//DNSCurve select signature request socket data of DNS target
PDNSCURVE_SERVER_DATA DNSCurveSelectSignatureTargetSocket(
	const uint16_t Protocol, 
	const bool IsAlternate, 
	size_t &ServerType, 
	std::vector<SOCKET_DATA> &SocketDataList)
{
	PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
	if (Protocol == AF_INET6)
	{
		if (IsAlternate)
		{
			((PSOCKADDR_IN6)&SocketDataList.front().SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&SocketDataList.front().SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6.AddressData.IPv6.sin6_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
			ServerType = DNSCURVE_ALTERNATE_IPV6;
		}
		else { //Main
			((PSOCKADDR_IN6)&SocketDataList.front().SockAddr)->sin6_addr = DNSCurveParameter.DNSCurve_Target_Server_Main_IPv6.AddressData.IPv6.sin6_addr;
			((PSOCKADDR_IN6)&SocketDataList.front().SockAddr)->sin6_port = DNSCurveParameter.DNSCurve_Target_Server_Main_IPv6.AddressData.IPv6.sin6_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Main_IPv6;
			ServerType = DNSCURVE_MAIN_IPV6;
		}

		SocketDataList.front().AddrLen = sizeof(sockaddr_in6);
		SocketDataList.front().SockAddr.ss_family = AF_INET6;
		return PacketTarget;
	}
	else if (Protocol == AF_INET)
	{
		if (IsAlternate)
		{
			((PSOCKADDR_IN)&SocketDataList.front().SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&SocketDataList.front().SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4.AddressData.IPv4.sin_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;
			ServerType = DNSCURVE_ALTERNATE_IPV4;
		}
		else { //Main
			((PSOCKADDR_IN)&SocketDataList.front().SockAddr)->sin_addr = DNSCurveParameter.DNSCurve_Target_Server_Main_IPv4.AddressData.IPv4.sin_addr;
			((PSOCKADDR_IN)&SocketDataList.front().SockAddr)->sin_port = DNSCurveParameter.DNSCurve_Target_Server_Main_IPv4.AddressData.IPv4.sin_port;
			PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Main_IPv4;
			ServerType = DNSCURVE_MAIN_IPV4;
		}

		SocketDataList.front().AddrLen = sizeof(sockaddr_in);
		SocketDataList.front().SockAddr.ss_family = AF_INET;
		return PacketTarget;
	}

	return nullptr;
}

//DNSCurve set packet target
bool DNSCurvePacketTargetSetting(
	const size_t ServerType, 
	DNSCURVE_SERVER_DATA ** const PacketTarget)
{
	switch (ServerType)
	{
		case DNSCURVE_ALTERNATE_IPV6:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
		}break;
		case DNSCURVE_MAIN_IPV6:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Main_IPv6;
		}break;
		case DNSCURVE_ALTERNATE_IPV4:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;
		}break;
		case DNSCURVE_MAIN_IPV4:
		{
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Main_IPv4;
		}break;
		default:
		{
			return false;
		}
	}

	return true;
}

//DNSCurve set Precomputation Key between client and server
bool DNSCurvePrecomputationKeySetting(
	uint8_t * const PrecomputationKey, 
	uint8_t * const Client_PublicKey, 
	const uint8_t * const ServerFingerprint)
{
//Server fingerprint check
	if (CheckEmptyBuffer(ServerFingerprint, crypto_box_PUBLICKEYBYTES))
	{
		return false;
	}
	else {
		sodium_memzero(PrecomputationKey, crypto_box_BEFORENMBYTES);
		sodium_memzero(Client_PublicKey, crypto_box_PUBLICKEYBYTES);
	}

//Make a client ephemeral key pair and a precomputation key.
	DNSCURVE_HEAP_BUFFER_TABLE<uint8_t> Client_SecretKey(crypto_box_SECRETKEYBYTES);
	if (crypto_box_keypair(
			Client_PublicKey, 
			Client_SecretKey.Buffer) != 0 || 
		crypto_box_beforenm(
			PrecomputationKey, 
			ServerFingerprint, 
			Client_SecretKey.Buffer) != 0)
				return false;

	return true;
}

//DNSCurve packet precomputation
void DNSCurveSocketPrecomputation(
	const uint16_t Protocol, 
	const uint8_t * const OriginalSend, 
	const size_t SendSize, 
	const size_t RecvSize, 
	uint8_t ** const PrecomputationKey, 
	uint8_t ** const Alternate_PrecomputationKey, 
	DNSCURVE_SERVER_DATA ** const PacketTarget, 
	std::vector<SOCKET_DATA> &SocketDataList, 
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> &SocketSelectingList, 
	std::shared_ptr<uint8_t> &SendBuffer, 
	size_t &DataLength, 
	std::shared_ptr<uint8_t> &Alternate_SendBuffer, 
	size_t &Alternate_DataLength)
{
//Selecting check
	bool *IsAlternate = nullptr;
	auto IsIPv6 = false;
	if (!DNSCurveSelectTargetSocket(Protocol, IsIPv6, &IsAlternate))
		return;

//Initialization
	SOCKET_DATA SocketDataTemp;
	DNSCURVE_SOCKET_SELECTING_DATA SocketSelectingDataTemp;
	memset(&SocketDataTemp, 0, sizeof(SocketDataTemp));
	memset(&SocketSelectingDataTemp, 0, sizeof(SocketSelectingDataTemp));
	std::vector<SOCKET_DATA> Alternate_SocketDataList;
	std::vector<DNSCURVE_SOCKET_SELECTING_DATA> Alternate_SocketSelectingList;
	uint8_t Client_PublicKey_PTR[crypto_box_PUBLICKEYBYTES]{0};
	auto Client_PublicKey = Client_PublicKey_PTR;
	size_t Index = 0, LoopLimits = 0;
	uint16_t InnerProtocol = 0;
	if (Protocol == IPPROTO_TCP)
		InnerProtocol = SOCK_STREAM;
	else if (Protocol == IPPROTO_UDP)
		InnerProtocol = SOCK_DGRAM;
	else 
		return;

//Main
	if (!*IsAlternate)
	{
	//Set target.
		if (IsIPv6)
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Main_IPv6;
		else //IPv4
			*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Main_IPv4;

	//Encryption mode check
		if (DNSCurveParameter.IsEncryption && 
			((!DNSCurveParameter.IsClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.IsClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer((*PacketTarget)->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
				goto SkipMain;

	//Socket initialization
		if (Protocol == IPPROTO_TCP)
			LoopLimits = Parameter.MultipleRequestTimes;
		else if (Protocol == IPPROTO_UDP)
			LoopLimits = 1U;
		else 
			goto SkipMain;
		for (Index = 0;Index < LoopLimits;++Index)
		{
			SocketDataTemp.SockAddr = (*PacketTarget)->AddressData.Storage;
			if (IsIPv6)
				SocketDataTemp.Socket = socket(AF_INET6, InnerProtocol, Protocol);
			else //IPv4
				SocketDataTemp.Socket = socket(AF_INET, InnerProtocol, Protocol);

		//Socket attribute settings
			if (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
				(InnerProtocol == IPPROTO_TCP && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_TCP_FAST_OPEN, true, nullptr)) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_NON_BLOCKING_MODE, true, nullptr) || 
				(IsIPv6 && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV6, true, nullptr)) || 
				(!IsIPv6 && (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV4, true, nullptr) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_DO_NOT_FRAGMENT, true, nullptr))))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();

				goto SkipMain;
			}

		//IPv6
			if (IsIPv6)
			{
				SocketDataTemp.AddrLen = sizeof(sockaddr_in6);
				SocketSelectingDataTemp.ServerType = DNSCURVE_MAIN_IPV6;
			}
		//IPv4
			else {
				SocketDataTemp.AddrLen = sizeof(sockaddr_in);
				SocketSelectingDataTemp.ServerType = DNSCURVE_MAIN_IPV4;
			}

			SocketDataList.push_back(SocketDataTemp);
			SocketSelectingList.push_back(SocketSelectingDataTemp);
			sodium_memzero(&SocketDataTemp, sizeof(SocketDataTemp));
		}

	//Make Precomputation Key between client and server.
		if (DNSCurveParameter.IsEncryption && DNSCurveParameter.IsClientEphemeralKey)
		{
			if (!DNSCurvePrecomputationKeySetting(*PrecomputationKey, Client_PublicKey, (*PacketTarget)->ServerFingerprint))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();

				goto SkipMain;
			}
		}
		else {
			Client_PublicKey = DNSCurveParameter.Client_PublicKey;
			*PrecomputationKey = (*PacketTarget)->PrecomputationKey;
		}

	//Make encryption or normal packet of Main server.
		if (DNSCurveParameter.IsEncryption || Protocol == IPPROTO_TCP)
		{
			std::shared_ptr<uint8_t> SendBufferTemp(new uint8_t[RecvSize]());
			sodium_memzero(SendBufferTemp.get(), RecvSize);
			SendBuffer.swap(SendBufferTemp);
			DataLength = DNSCurvePacketEncryption(Protocol, (*PacketTarget)->SendMagicNumber, Client_PublicKey, *PrecomputationKey, OriginalSend, SendSize, SendBuffer.get(), RecvSize);
			if (DataLength < DNS_PACKET_MINSIZE)
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;

				goto SkipMain;
			}
		}
	}

//Jump here to skip Main process
SkipMain:
	sodium_memzero(&SocketDataTemp, sizeof(SocketDataTemp));

//Set target.
	if (IsIPv6)
		*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv6;
	else //IPv4
		*PacketTarget = &DNSCurveParameter.DNSCurve_Target_Server_Alternate_IPv4;

//Alternate
	if ((*PacketTarget)->AddressData.Storage.ss_family > 0 && (*IsAlternate || Parameter.AlternateMultipleRequest))
	{
	//Encryption mode check
		if (DNSCurveParameter.IsEncryption && 
			((!DNSCurveParameter.IsClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->PrecomputationKey, crypto_box_BEFORENMBYTES)) || 
			(DNSCurveParameter.IsClientEphemeralKey && CheckEmptyBuffer((*PacketTarget)->ServerFingerprint, crypto_box_PUBLICKEYBYTES)) || 
			CheckEmptyBuffer((*PacketTarget)->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN)))
		{
			for (auto &SocketDataIter:SocketDataList)
				SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			SocketDataList.clear();
			SocketSelectingList.clear();
			DataLength = 0;

			return;
		}

	//Socket initialization
		if (Protocol == IPPROTO_TCP)
		{
			LoopLimits = Parameter.MultipleRequestTimes;
		}
		else if (Protocol == IPPROTO_UDP)
		{
			LoopLimits = 1U;
		}
		else {
			for (auto &SocketDataIter:SocketDataList)
				SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
			SocketDataList.clear();
			SocketSelectingList.clear();
			DataLength = 0;

			return;
		}
		for (Index = 0;Index < LoopLimits;++Index)
		{
			SocketDataTemp.SockAddr = (*PacketTarget)->AddressData.Storage;
			if (IsIPv6)
				SocketDataTemp.Socket = socket(AF_INET6, InnerProtocol, Protocol);
			else //IPv4
				SocketDataTemp.Socket = socket(AF_INET, InnerProtocol, Protocol);

		//Socket attribute settings
			if (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_INVALID_CHECK, true, nullptr) || 
				(Protocol == IPPROTO_TCP && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_TCP_FAST_OPEN, true, nullptr)) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_NON_BLOCKING_MODE, true, nullptr) || 
				(IsIPv6 && !SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV6, true, nullptr)) || 
				(!IsIPv6 && (!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_HOP_LIMITS_IPV4, true, nullptr) || 
				!SocketSetting(SocketDataTemp.Socket, SOCKET_SETTING_DO_NOT_FRAGMENT, true, nullptr))))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;
				for (auto &SocketDataIter:Alternate_SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				Alternate_SocketDataList.clear();
				Alternate_SocketSelectingList.clear();

				return;
			}

		//IPv6
			if (IsIPv6)
			{
				SocketDataTemp.AddrLen = sizeof(sockaddr_in6);
				SocketSelectingDataTemp.ServerType = DNSCURVE_ALTERNATE_IPV6;
			}
		//IPv4
			else {
				SocketDataTemp.AddrLen = sizeof(sockaddr_in);
				SocketSelectingDataTemp.ServerType = DNSCURVE_ALTERNATE_IPV4;
			}

			Alternate_SocketDataList.push_back(SocketDataTemp);
			Alternate_SocketSelectingList.push_back(SocketSelectingDataTemp);
			sodium_memzero(&SocketDataTemp, sizeof(SocketDataTemp));
		}

	//Make Precomputation Key between client and server.
		if (DNSCurveParameter.IsEncryption && DNSCurveParameter.IsClientEphemeralKey)
		{
			if (!DNSCurvePrecomputationKeySetting(*Alternate_PrecomputationKey, Client_PublicKey, (*PacketTarget)->ServerFingerprint))
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;
				for (auto &SocketDataIter:Alternate_SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				Alternate_SocketDataList.clear();
				Alternate_SocketSelectingList.clear();

				return;
			}
		}
		else {
			Client_PublicKey = DNSCurveParameter.Client_PublicKey;
			*Alternate_PrecomputationKey = (*PacketTarget)->PrecomputationKey;
		}

	//Make encryption or normal packet of Alternate server.
		if (DNSCurveParameter.IsEncryption)
		{
			std::shared_ptr<uint8_t> SendBufferTemp(new uint8_t[RecvSize]());
			sodium_memzero(SendBufferTemp.get(), RecvSize);
			Alternate_SendBuffer.swap(SendBufferTemp);
			SendBufferTemp.reset();
			Alternate_DataLength = DNSCurvePacketEncryption(Protocol, (*PacketTarget)->SendMagicNumber, Client_PublicKey, *Alternate_PrecomputationKey, OriginalSend, SendSize, Alternate_SendBuffer.get(), RecvSize);
			if (Alternate_DataLength < DNS_PACKET_MINSIZE)
			{
				for (auto &SocketDataIter:SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				SocketDataList.clear();
				SocketSelectingList.clear();
				DataLength = 0;
				for (auto &SocketDataIter:Alternate_SocketDataList)
					SocketSetting(SocketDataIter.Socket, SOCKET_SETTING_CLOSE, false, nullptr);
				Alternate_SocketDataList.clear();
				Alternate_SocketSelectingList.clear();
				Alternate_DataLength = 0;

				return;
			}
		}

	//Add to global list.
		if (!Alternate_SocketDataList.empty() && !Alternate_SocketSelectingList.empty())
		{
			for (auto &SocketDataIter:Alternate_SocketDataList)
				SocketDataList.push_back(SocketDataIter);
			for (auto &SocketSelectingIter:Alternate_SocketSelectingList)
				SocketSelectingList.push_back(SocketSelectingIter);
		}
	}

	return;
}

//DNSCurve packet encryption
size_t DNSCurvePacketEncryption(
	const uint16_t Protocol, 
	const uint8_t * const SendMagicNumber, 
	const uint8_t * const Client_PublicKey, 
	const uint8_t * const PrecomputationKey, 
	const uint8_t * const OriginalSend, 
	const size_t Length, 
	uint8_t * const SendBuffer, 
	const size_t SendSize)
{
//Encryption mode
	if (DNSCurveParameter.IsEncryption)
	{
		uint8_t Nonce[crypto_box_NONCEBYTES]{0};

	//Make nonce.
		*(uint32_t *)Nonce = randombytes_random();
		*(uint32_t *)(Nonce + sizeof(uint32_t)) = randombytes_random();
		*(uint32_t *)(Nonce + sizeof(uint32_t) * 2U) = randombytes_random();
		sodium_memzero(Nonce + crypto_box_HALF_NONCEBYTES, crypto_box_HALF_NONCEBYTES);

	//Make a crypto box.
		std::shared_ptr<uint8_t> Buffer;
		if (Protocol == IPPROTO_TCP)
		{
			std::shared_ptr<uint8_t> BufferTemp(new uint8_t[DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN]());
			sodium_memzero(BufferTemp.get(), DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN);
			Buffer.swap(BufferTemp);
		}
		else if (Protocol == IPPROTO_UDP)
		{
			std::shared_ptr<uint8_t> BufferTemp(new uint8_t[DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN]());
			sodium_memzero(BufferTemp.get(), DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN);
			Buffer.swap(BufferTemp);
		}
		else {
			return EXIT_FAILURE;
		}

		memcpy_s(Buffer.get() + crypto_box_ZEROBYTES, DNSCurveParameter.DNSCurvePayloadSize - crypto_box_ZEROBYTES, OriginalSend, Length);
		DNSCurvePaddingData(true, Buffer.get(), crypto_box_ZEROBYTES + Length);

	//Encrypt data.
		if (Protocol == IPPROTO_TCP)
		{
			if (crypto_box_afternm(
					SendBuffer + DNSCRYPT_BUFFER_RESERVE_TCP_LEN, 
					Buffer.get(), 
					DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_TCP_LEN, 
					Nonce, 
					PrecomputationKey) != 0)
						return EXIT_FAILURE;
		}
		else if (Protocol == IPPROTO_UDP)
		{
			if (crypto_box_afternm(
					SendBuffer + DNSCRYPT_BUFFER_RESERVE_LEN, 
					Buffer.get(), 
					DNSCurveParameter.DNSCurvePayloadSize - DNSCRYPT_BUFFER_RESERVE_LEN, 
					Nonce, 
					PrecomputationKey) != 0)
						return EXIT_FAILURE;
		}
		else {
			return EXIT_FAILURE;
		}

	//Make DNSCurve encryption packet.
		Buffer.reset();
		if (Protocol == IPPROTO_TCP)
		{
			memcpy_s(SendBuffer + sizeof(uint16_t), SendSize - sizeof(uint16_t), SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN);
			memcpy_s(SendBuffer + sizeof(uint16_t) + DNSCURVE_MAGIC_QUERY_LEN, SendSize - sizeof(uint16_t) - DNSCURVE_MAGIC_QUERY_LEN, Client_PublicKey, crypto_box_PUBLICKEYBYTES);
			memcpy_s(SendBuffer + sizeof(uint16_t) + DNSCURVE_MAGIC_QUERY_LEN + crypto_box_PUBLICKEYBYTES, SendSize - sizeof(uint16_t) - DNSCURVE_MAGIC_QUERY_LEN - crypto_box_PUBLICKEYBYTES, Nonce, crypto_box_HALF_NONCEBYTES);

		//Add length of request packet(It must be written in header when transport with TCP protocol).
			*(uint16_t *)SendBuffer = htons((uint16_t)(DNSCurveParameter.DNSCurvePayloadSize - sizeof(uint16_t)));
		}
		else if (Protocol == IPPROTO_UDP)
		{
			memcpy_s(SendBuffer, SendSize, SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN);
			memcpy_s(SendBuffer + DNSCURVE_MAGIC_QUERY_LEN, SendSize - DNSCURVE_MAGIC_QUERY_LEN, Client_PublicKey, crypto_box_PUBLICKEYBYTES);
			memcpy_s(SendBuffer + DNSCURVE_MAGIC_QUERY_LEN + crypto_box_PUBLICKEYBYTES, SendSize - DNSCURVE_MAGIC_QUERY_LEN - crypto_box_PUBLICKEYBYTES, Nonce, crypto_box_HALF_NONCEBYTES);
		}
		else {
			return EXIT_FAILURE;
		}

		return DNSCurveParameter.DNSCurvePayloadSize;
	}
//Normal mode
	else {
		memcpy_s(SendBuffer, SendSize, OriginalSend, Length);
		if (Protocol == IPPROTO_TCP)
			return AddLengthDataToHeader(SendBuffer, Length, SendSize); //Add length of request packet(It must be written in header when transport with TCP protocol).
		else if (Protocol == IPPROTO_UDP)
			return Length;
	}

	return EXIT_FAILURE;
}

//DNSCurve packet decryption
ssize_t DNSCurvePacketDecryption(
	const uint8_t * const ReceiveMagicNumber, 
	const uint8_t * const PrecomputationKey, 
	uint8_t * const OriginalRecv, 
	const size_t RecvSize, 
	const ssize_t Length)
{
	ssize_t DataLength = Length;

//Encryption mode
	if (DNSCurveParameter.IsEncryption)
	{
	//Receive Magic number check
		sodium_memzero(OriginalRecv + Length, RecvSize - Length);
		if (sodium_memcmp(OriginalRecv, ReceiveMagicNumber, DNSCURVE_MAGIC_QUERY_LEN) != 0)
			return EXIT_FAILURE;

	//Nonce initialization
		uint8_t WholeNonce[crypto_box_NONCEBYTES]{0};
		memcpy_s(WholeNonce, crypto_box_NONCEBYTES, OriginalRecv + DNSCURVE_MAGIC_QUERY_LEN, crypto_box_NONCEBYTES);

	//Open crypto box.
		sodium_memzero(OriginalRecv, DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES);
		memmove_s(OriginalRecv + crypto_box_BOXZEROBYTES, RecvSize - crypto_box_BOXZEROBYTES, OriginalRecv + DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES, Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES));
		if (crypto_box_open_afternm(
			(unsigned char *)OriginalRecv, 
			(unsigned char *)OriginalRecv, 
			Length + crypto_box_BOXZEROBYTES - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES), 
			WholeNonce, 
			PrecomputationKey) != 0)
				return EXIT_FAILURE;
		memmove_s(OriginalRecv, RecvSize, OriginalRecv + crypto_box_ZEROBYTES, Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES));
		sodium_memzero(OriginalRecv + Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES), RecvSize - (Length - (DNSCURVE_MAGIC_QUERY_LEN + crypto_box_NONCEBYTES)));

	//Check padding data and responses check.
		DataLength = DNSCurvePaddingData(false, OriginalRecv, Length);
		if (DataLength < (ssize_t)DNS_PACKET_MINSIZE)
			return EXIT_FAILURE;
	}

//Response check
	DataLength = CheckResponseData(
		REQUEST_PROCESS_DNSCURVE_MAIN, 
		OriginalRecv, 
		DataLength, 
		RecvSize, 
		nullptr);
	if (DataLength < (ssize_t)DNS_PACKET_MINSIZE)
		return EXIT_FAILURE;

	return DataLength;
}

//Get Signature Data of server from packets
bool DNSCruveGetSignatureData(
	const uint8_t * const Buffer, 
	const size_t ServerType)
{
	if (ntohs(((pdns_record_txt)Buffer)->Name) == DNS_POINTER_QUERY && 
		ntohs(((pdns_record_txt)Buffer)->Length) == ((pdns_record_txt)Buffer)->TXT_Length + 1U && ((pdns_record_txt)Buffer)->TXT_Length == DNSCRYPT_RECORD_TXT_LEN)
	{
		if (sodium_memcmp(&((pdnscurve_txt_hdr)(Buffer + sizeof(dns_record_txt)))->CertMagicNumber, DNSCRYPT_CERT_MAGIC, sizeof(uint16_t)) == 0 && 
			ntohs(((pdnscurve_txt_hdr)(Buffer + sizeof(dns_record_txt)))->MajorVersion) == DNSCURVE_VERSION_MAJOR && 
			ntohs(((pdnscurve_txt_hdr)(Buffer + sizeof(dns_record_txt)))->MinorVersion) == DNSCURVE_VERSION_MINOR)
		{
		//Get Send Magic Number, Server Fingerprint and Precomputation Key.
			PDNSCURVE_SERVER_DATA PacketTarget = nullptr;
			if (!DNSCurvePacketTargetSetting(ServerType, &PacketTarget))
				return false;

		//Check Signature.
			std::shared_ptr<uint8_t> DeBuffer(new uint8_t[PACKET_MAXSIZE]());
			memset(DeBuffer.get(), 0, PACKET_MAXSIZE);
			unsigned long long SignatureLength = 0;
			if (PacketTarget == nullptr || 
				crypto_sign_open(
					(unsigned char *)DeBuffer.get(), 
					&SignatureLength, 
					(unsigned char *)(Buffer + sizeof(dns_record_txt) + sizeof(dnscurve_txt_hdr)), ((pdns_record_txt)Buffer)->TXT_Length - sizeof(dnscurve_txt_hdr), 
					PacketTarget->ServerPublicKey) != 0)
			{
				std::wstring Message;
				DNSCurvePrintLog(ServerType, Message);
				if (!Message.empty())
				{
					Message.append(L"Fingerprint signature validation error");
					PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
				}

				return false;
			}

		//Signature available time check
			const auto TimeValues = time(nullptr);
			if (PacketTarget->ServerFingerprint != nullptr && 
				TimeValues >= (time_t)ntohl(((pdnscurve_txt_signature)DeBuffer.get())->CertTime_Begin) && 
				TimeValues <= (time_t)ntohl(((pdnscurve_txt_signature)DeBuffer.get())->CertTime_End))
			{
				memcpy_s(PacketTarget->SendMagicNumber, DNSCURVE_MAGIC_QUERY_LEN, ((pdnscurve_txt_signature)DeBuffer.get())->MagicNumber, DNSCURVE_MAGIC_QUERY_LEN);
				memcpy_s(PacketTarget->ServerFingerprint, crypto_box_PUBLICKEYBYTES, ((pdnscurve_txt_signature)DeBuffer.get())->PublicKey, crypto_box_PUBLICKEYBYTES);
				if (!DNSCurveParameter.IsClientEphemeralKey)
				{
					if (crypto_box_beforenm(
							PacketTarget->PrecomputationKey, 
							PacketTarget->ServerFingerprint, 
							DNSCurveParameter.Client_SecretKey) != 0)
					{
						std::wstring Message;
						DNSCurvePrintLog(ServerType, Message);
						if (!Message.empty())
						{
							Message.append(L"Key calculating error");
							PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
						}

						return false;
					}
				}

				return true;
			}
			else {
				std::wstring Message;
				DNSCurvePrintLog(ServerType, Message);
				if (!Message.empty())
				{
					Message.append(L"Fingerprint signature validation error");
					PrintError(LOG_LEVEL_3, LOG_ERROR_DNSCURVE, Message.c_str(), 0, nullptr, 0);
				}
			}
		}
	}

	return false;
}
#endif