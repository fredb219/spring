/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#if   defined(_MSC_VER)
#	include "StdAfx.h"
#elif defined(_WIN32)
#	include <windows.h>
#endif

#include <SDL_timer.h>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "mmgr.h"

// NOTE: these _must_ be included before NetProtocol.h due to some ambiguity in
// Boost hash_float.hpp ("call of overloaded ‘ldexp(float&, int&)’ is ambiguous")
#include "System/Net/UDPConnection.h"
#include "System/Net/LocalConnection.h"
#include "NetProtocol.h"

#include "Game/GameData.h"
#include "Game/GlobalUnsynced.h"
#include "Sim/Misc/GlobalConstants.h"
#include "System/Net/UnpackPacket.h"
#include "System/LoadSave/DemoRecorder.h"
#include "System/ConfigHandler.h"
#include "System/Log/ILog.h"
#include "lib/gml/gmlmut.h"


CNetProtocol::CNetProtocol() : loading(false), disableDemo(false)
{
}

CNetProtocol::~CNetProtocol()
{
	Send(CBaseNetProtocol::Get().SendQuit(""));
	LOG("%s", serverConn->Statistics().c_str());
}

void CNetProtocol::InitClient(const char* server_addr, unsigned portnum, const std::string& myName, const std::string& myPasswd, const std::string& myVersion)
{
	GML_STDMUTEX_LOCK(net); // InitClient

	netcode::UDPConnection* conn = new netcode::UDPConnection(configHandler->Get("SourcePort", 0), server_addr, portnum);
	serverConn.reset(conn);
	serverConn->SendData(CBaseNetProtocol::Get().SendAttemptConnect(myName, myPasswd, myVersion));
	serverConn->Flush(true);

	LOG("Connecting to %s:%i using name %s", server_addr, portnum, myName.c_str());
}

void CNetProtocol::AttemptReconnect(const std::string& myName, const std::string& myPasswd, const std::string& myVersion) {
	GML_STDMUTEX_LOCK(net); // AttemptReconnect

	netcode::UDPConnection* conn = new netcode::UDPConnection(*serverConn);
	conn->SendData(CBaseNetProtocol::Get().SendAttemptConnect(myName, myPasswd, myVersion, true));
	conn->Flush(true);

	LOG("Reconnecting to server... %ds", dynamic_cast<netcode::UDPConnection&>(*serverConn).GetReconnectSecs());

	delete conn;
}

bool CNetProtocol::NeedsReconnect() {
	return serverConn->NeedsReconnect();
}

void CNetProtocol::InitLocalClient()
{
	GML_STDMUTEX_LOCK(net); // InitLocalClient

	serverConn.reset(new netcode::CLocalConnection);
	serverConn->Flush();

	LOG("Connecting to local server");
}

bool CNetProtocol::CheckTimeout(int nsecs, bool initial) const {
	return serverConn->CheckTimeout(nsecs, initial);
}

bool CNetProtocol::Connected() const
{
	return (serverConn->GetDataReceived() > 0);
}

std::string CNetProtocol::ConnectionStr() const
{
	return serverConn->GetFullAddress();
}

boost::shared_ptr<const netcode::RawPacket> CNetProtocol::Peek(unsigned ahead) const
{
	GML_STDMUTEX_LOCK(net); // Peek

	return serverConn->Peek(ahead);
}

void CNetProtocol::DeleteBufferPacketAt(unsigned index)
{
	GML_STDMUTEX_LOCK(net); // DeleteBufferPacketAt

	return serverConn->DeleteBufferPacketAt(index);
}

boost::shared_ptr<const netcode::RawPacket> CNetProtocol::GetData(int framenum)
{
	GML_STDMUTEX_LOCK(net); // GetData

	boost::shared_ptr<const netcode::RawPacket> ret = serverConn->GetData();

	if (ret) {
		float demoTime = (framenum == 0) ? gu->gameTime : gu->startTime + (float)framenum / (float)GAME_SPEED;
		if (record) {
			record->SaveToDemo(ret->data, ret->length, demoTime);
		} else if (ret->data[0] == NETMSG_GAMEDATA && !disableDemo) {
			try {
				GameData gd(ret);

				LOG("Starting demo recording");
				record.reset(new CDemoRecorder());
				record->WriteSetupText(gd.GetSetup());
				record->SaveToDemo(ret->data, ret->length, demoTime);
			} catch (netcode::UnpackPacketException &e) {
				LOG_L(L_WARNING, "Invalid GameData received: %s", e.err.c_str());
			}
		}
	}

	return ret;
}

void CNetProtocol::Send(boost::shared_ptr<const netcode::RawPacket> pkt)
{
	GML_STDMUTEX_LOCK(net); // Send

	serverConn->SendData(pkt);
}

void CNetProtocol::Send(const netcode::RawPacket* pkt)
{
	boost::shared_ptr<const netcode::RawPacket> ptr(pkt);
	Send(ptr);
}

void CNetProtocol::UpdateLoop()
{
	loading = true;
	while (loading) {
		Update();
		SDL_Delay(400);
	}
}

void CNetProtocol::Update()
{
	GML_STDMUTEX_LOCK(net); // Update

	serverConn->Update();
}

void CNetProtocol::DisableDemoRecording()
{
	disableDemo = true;
}

CNetProtocol* net = NULL;

