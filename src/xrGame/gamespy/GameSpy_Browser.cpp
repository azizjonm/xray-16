#include "StdAfx.h"
#include "game_base_space.h"
#include "xrServerEntities/gametype_chooser.h"
#include "GameSpy_Browser.h"
#include "GameSpy_Base_Defs.h"
#include "ui/ServerList.h"
#include "GameSpy_Available.h"
#include "GameSpy_QR2.h"
#include "Common/object_broker.h"

void __cdecl SBCallback(ServerBrowser sb, SBCallbackReason reason, SBServer server, void *instance);
EGameIDs ParseStringToGameType(LPCSTR str);

CGameSpy_Browser::CGameSpy_Browser()
#ifdef CONFIG_PROFILE_LOCKS
	:m_refresh_lock(MUTEX_PROFILE_ID(CGameSpy_Browser::m_refresh_lock))
#endif // CONFIG_PROFILE_LOCKS

{
	//-------------------------
	m_hGameSpyDLL = NULL;
	m_pQR2 = NULL;
	m_pGSBrowser = NULL;
	m_pServerList = NULL;
	//-------------------------
	LPCSTR			g_name	= "xrGameSpy";
	Log				("Loading DLL:",g_name);
	m_hGameSpyDLL			= LoadLibrary	(g_name);
	if (0==m_hGameSpyDLL)	R_CHK			(GetLastError());
	R_ASSERT2		(m_hGameSpyDLL,"GameSpy DLL raised exception during loading or there is no game DLL at all");
	//-------------------------
	LoadGameSpy(m_hGameSpyDLL);
	//-------------------------
	InitInternalData(m_hGameSpyDLL);
};

CGameSpy_Browser::CGameSpy_Browser(HMODULE hGameSpyDLL)
#ifdef CONFIG_PROFILE_LOCKS
	:m_refresh_lock(MUTEX_PROFILE_ID(CGameSpy_Browser::m_refresh_lock))
#endif // CONFIG_PROFILE_LOCKS

{
	//-------------------------
	m_hGameSpyDLL = NULL;
	m_pQR2 = NULL;
	m_pGSBrowser = NULL;
	m_pServerList = NULL;
	//-------------------------
	LoadGameSpy(hGameSpyDLL);
	//-------------------------
	InitInternalData(hGameSpyDLL);
};

void	CGameSpy_Browser::InitInternalData(HMODULE hGameSpyDLL)
{
	m_pQR2	= xr_new<CGameSpy_QR2>(hGameSpyDLL);
	m_pQR2->RegisterAdditionalKeys();

	m_bAbleToConnectToMasterServer = true;
	m_bTryingToConnectToMasterServer = false;
	m_bShowCMSErr = false;

	m_pGSBrowser = xrGS_ServerBrowserNewA(SBFalse, SBCallback, this);
	if (!m_pGSBrowser)
	{
		Msg("! Unable to init Server Browser!");
	}
	//	else
	//		Msg("- GS Server Browser Inited!");


};

CGameSpy_Browser::~CGameSpy_Browser()
{
	if ( m_pServerList )
	{
		m_pServerList->on_game_spy_browser_destroy	(this);
	}	

	Clear();

	delete_data(m_pQR2);
	if (m_pGSBrowser)
	{		
		xrGS_ServerBrowserFree(m_pGSBrowser);
		m_pGSBrowser = NULL;
	}

	if (m_hGameSpyDLL)
	{
		FreeLibrary(m_hGameSpyDLL);
		m_hGameSpyDLL = NULL;
	}	
};

void	CGameSpy_Browser::LoadGameSpy(HMODULE hGameSpyDLL)
{	
	GAMESPY_LOAD_FN(xrGS_ServerBrowserNewA);	
	GAMESPY_LOAD_FN(xrGS_ServerBrowserFree);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserClear);
	
	GAMESPY_LOAD_FN(xrGS_ServerBrowserThink);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserState);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserHalt);	
	GAMESPY_LOAD_FN(xrGS_ServerBrowserUpdateA);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserLANUpdate);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserCount);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserGetServer);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserGetServerByIPA);

	GAMESPY_LOAD_FN(xrGS_SBServerGetPublicAddress);
	GAMESPY_LOAD_FN(xrGS_SBServerGetPublicQueryPort);
	GAMESPY_LOAD_FN(xrGS_SBServerGetStringValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetIntValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetFloatValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetBoolValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetPing);

	GAMESPY_LOAD_FN(xrGS_ServerBrowserAuxUpdateServer);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserAuxUpdateIPA);
	
	GAMESPY_LOAD_FN(xrGS_SBServerGetPlayerStringValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetPlayerIntValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetPlayerFloatValueA);

	GAMESPY_LOAD_FN(xrGS_SBServerGetTeamStringValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetTeamIntValueA);
	GAMESPY_LOAD_FN(xrGS_SBServerGetTeamFloatValueA);

	GAMESPY_LOAD_FN(xrGS_ServerBrowserRemoveIPA);
	GAMESPY_LOAD_FN(xrGS_ServerBrowserRemoveServer);

	GAMESPY_LOAD_FN(xrGS_SBServerGetConnectionInfo);
	GAMESPY_LOAD_FN(xrGS_SBServerDirectConnect);

	GAMESPY_LOAD_FN(xrGS_ServerBrowserSortA);

	GAMESPY_LOAD_FN(xrGS_SBServerHasFullKeys);

	GAMESPY_LOAD_FN(xrGS_GetGameID);

	GAMESPY_LOAD_FN(xrGS_ServerBrowserErrorDescA);
}

static bool services_checked = false;

bool	CGameSpy_Browser::Init(CServerList* pServerList)
{
	if (m_pServerList)
	{
		m_pServerList->on_game_spy_browser_destroy(this);
	}
	m_pServerList = pServerList;
	return true;
};

void	CGameSpy_Browser::Clear()
{
	m_pServerList = NULL;
};

struct RefreshData
{
	CGameSpy_Browser*	pGSBrowser;
	string4096			FilterStr;
};
void	RefreshInternetList	(void * inData)
{
	RefreshData* pRData = (RefreshData*) inData;
	pRData->pGSBrowser->RefreshListInternet(pRData->FilterStr);
	xr_delete(pRData);
};

void			CGameSpy_Browser::RefreshListInternet (const char* FilterStr)
{
	m_refresh_lock.Enter();

	SBError error = sbe_noerror;
	error = xrGS_ServerBrowserUpdateA(m_pGSBrowser, m_pServerList ? SBTrue : SBFalse, SBFalse, FilterStr);
	m_bAbleToConnectToMasterServer = (error == sbe_noerror);
	m_bShowCMSErr = (error != sbe_noerror);
	m_bTryingToConnectToMasterServer = false;

	m_refresh_lock.Leave();
};

GSUpdateStatus CGameSpy_Browser::RefreshList_Full(bool Local, const char* FilterStr)
{
	if (!m_pGSBrowser)
        return GSUpdateStatus::Success;
	SBState state = xrGS_ServerBrowserState(m_pGSBrowser);               
	if((state != sb_connected) && (state != sb_disconnected))
	{
		xrGS_ServerBrowserHalt(m_pGSBrowser);
		Msg("xrGSB Refresh Stopped\n");		
	};
	xrGS_ServerBrowserClear(m_pGSBrowser);

	// do an update
    if (!Local)
    {
        m_refresh_lock.Enter();
        m_refresh_lock.Leave();
		if (!m_bAbleToConnectToMasterServer)
		    return GSUpdateStatus::MasterUnreachable;
        RefreshData*	pRData = xr_new<RefreshData>();
        xr_strcpy(pRData->FilterStr, FilterStr);
        pRData->pGSBrowser = this;
        m_bTryingToConnectToMasterServer = true;
        thread_spawn(RefreshInternetList, "GS Internet Refresh", 0, pRData);
        return GSUpdateStatus::ConnectingToMaster;
    }
    SBError error = xrGS_ServerBrowserLANUpdate(m_pGSBrowser, m_pServerList ? SBTrue : SBFalse);
    if (error!=sbe_noerror)
    {
        Msg("! xrGSB Error - %s", xrGS_ServerBrowserErrorDescA(m_pGSBrowser, error));
        return GSUpdateStatus::Unknown;
    }
    return GSUpdateStatus::Success;
};

void __cdecl SBCallback(ServerBrowser sb, SBCallbackReason reason, SBServer server, void *instance)
{
	CGameSpy_Browser* pGSBrowser = (CGameSpy_Browser*) instance;
	if (!pGSBrowser) return;
	switch (reason)
	{
	case sbc_serveradded : //a server was added to the list, may just have an IP & port at this point
		{
#ifdef _DEBUG
//.			Msg("sbc_serveradded");
#endif
//			pGSBrowser->SortBrowserByPing();
//			pGSBrowser->UpdateServerList();			
		}break;
	case sbc_serverupdated : //server information has been updated - either basic or full information is now available about this server
		{
#ifdef _DEBUG
//.			Msg("sbc_serverupdated");
#endif
//			pGSBrowser->SortBrowserByPing();
			pGSBrowser->UpdateServerList();
		}break;
	case sbc_serverupdatefailed : //an attempt to retrieve information about this server, either directly or from the master, failed
		{
#ifdef _DEBUG
//.			Msg("sbc_serverupdatefailed");
#endif
//			pGSBrowser->OnUpdateFailed(server);
//			pGSBrowser->SortBrowserByPing();
			pGSBrowser->UpdateServerList();
		}break;
	case sbc_serverdeleted : //a server was removed from the list
		{
#ifdef _DEBUG
			Msg("sbc_serverdeleted");
#endif
//			pGSBrowser->SortBrowserByPing();
			pGSBrowser->UpdateServerList();
		}break;
	case sbc_updatecomplete : //the server query engine is now idle 
		{
#ifdef _DEBUG
//.			Msg("sbc_updatecomplete");
#endif
//			pGSBrowser->SortBrowserByPing();
			pGSBrowser->UpdateServerList();
		}break;
	case sbc_queryerror		://the master returned an error string for the provided query
		{
#ifdef _DEBUG
			Msg("sbc_queryerror");
#endif
		}break;
	case sbc_serverchallengereceived:
		{
#ifdef _DEBUG
//.			Msg("sbc_serverchallengereceived");
#endif
		}break;
	default:
		{
			R_ASSERT2(0, "Unknown Callback Reason");
		}break;
	};
};

void CGameSpy_Browser::CallBack_OnUpdateCompleted()
{
	int NumServers = xrGS_ServerBrowserCount(m_pGSBrowser);

	ServerInfo NewServerInfo;
	for (int i=0; i<NumServers; i++)
	{
		void* pServer = xrGS_ServerBrowserGetServer(m_pGSBrowser, i);
		ReadServerInfo(&NewServerInfo, pServer);
	}
};

int	CGameSpy_Browser::GetServersCount()
{
	return xrGS_ServerBrowserCount(m_pGSBrowser);
};

void CGameSpy_Browser::GetServerInfoByIndex(ServerInfo* pServerInfo, int idx)
{
    void *pServer = GetServerByIndex(idx);
	ReadServerInfo(pServerInfo, pServer);
	pServerInfo->Index = idx;
}

void *CGameSpy_Browser::GetServerByIndex(int index)
{ return xrGS_ServerBrowserGetServer(m_pGSBrowser, index); }

bool CGameSpy_Browser::GetBool(void *srv, int keyId, bool defaultValue)
{
    const char *key = m_pQR2->xrGS_RegisteredKey(keyId);
    return xrGS_SBServerGetBoolValueA(srv, key, defaultValue ? SBTrue : SBFalse)==SBTrue;
}

int CGameSpy_Browser::GetInt(void *srv, int keyId, int defaultValue)
{
    const char *key = m_pQR2->xrGS_RegisteredKey(keyId);
    return xrGS_SBServerGetIntValueA(srv, key, defaultValue);
}

float CGameSpy_Browser::GetFloat(void *srv, int keyId, float defaultValue)
{
    const char *key = m_pQR2->xrGS_RegisteredKey(keyId);
    return xrGS_SBServerGetFloatValueA(srv, key, defaultValue);
}

const char *CGameSpy_Browser::GetString(void *srv, int keyId, const char *defaultValue)
{
    const char *key = m_pQR2->xrGS_RegisteredKey(keyId);
    return xrGS_SBServerGetStringValueA(srv, key, defaultValue);
}

void	CGameSpy_Browser::ReadServerInfo	(ServerInfo* pServerInfo, void* pServer)
{
	if (!pServer || !pServerInfo) return;
	xr_sprintf(pServerInfo->m_Address, "%s:%d", xrGS_SBServerGetPublicAddress(pServer), xrGS_SBServerGetPublicQueryPort(pServer));
	xr_sprintf(pServerInfo->m_HostName, "%s", xrGS_SBServerGetPublicAddress(pServer));
	xr_sprintf(pServerInfo->m_ServerName, "%s", GetString(pServer, HOSTNAME_KEY, pServerInfo->m_HostName));
	xr_sprintf(pServerInfo->m_SessionName, "%s", GetString(pServer, MAPNAME_KEY, "Unknown"));
	xr_sprintf(pServerInfo->m_ServerGameType, "%s", GetString(pServer, GAMETYPE_KEY, "Unknown"));
	pServerInfo->m_bPassword = GetBool(pServer, PASSWORD_KEY);
	pServerInfo->m_bUserPass = GetBool(pServer, G_USER_PASSWORD_KEY);
	pServerInfo->m_Ping = (s16)(xrGS_SBServerGetPing(pServer) & 0xffff);
	pServerInfo->m_ServerNumPlayers = (s16)GetInt(pServer, NUMPLAYERS_KEY, 0);
	pServerInfo->m_ServerMaxPlayers = (s16)GetInt(pServer, MAXPLAYERS_KEY, 32);
	xr_sprintf(pServerInfo->m_ServerUpTime, "%s", GetString(pServer, SERVER_UP_TIME_KEY, "Unknown"));
	pServerInfo->m_ServerNumTeams = (s16)GetInt(pServer, NUMTEAMS_KEY, 0);
	pServerInfo->m_Port = (s16)GetInt(pServer, HOSTPORT_KEY, 0);
	pServerInfo->m_HPort = (s16)xrGS_SBServerGetPublicQueryPort(pServer);
	pServerInfo->m_bDedicated = GetBool(pServer, DEDICATED_KEY);
	pServerInfo->m_GameType = (u8)GetInt(pServer, GAMETYPE_NAME_KEY, 0);
	if (pServerInfo->m_GameType == 0)
	{
		pServerInfo->m_GameType = ParseStringToGameType(pServerInfo->m_ServerGameType);
	}
	xr_sprintf(pServerInfo->m_ServerVersion, "%s", GetString(pServer, GAMEVER_KEY, "--"));

	//--------- Read Game Infos ---------------------------//
	pServerInfo->m_aPlayers.clear();
	pServerInfo->m_aTeams.clear();
	//-------------------------------------------------------//
	if (xrGS_SBServerHasFullKeys(pServer) == SBFalse) return;
    
    pServerInfo->MaxPing = GetInt(pServer, G_MAX_PING_KEY);
    pServerInfo->MapRotation = GetBool(pServer, G_MAP_ROTATION_KEY);
    pServerInfo->VotingEnabled = GetBool(pServer, G_VOTING_ENABLED_KEY);
    pServerInfo->SpectratorModes = GetInt(pServer, G_SPECTATOR_MODES_KEY);    
    if (pServerInfo->m_GameType == eGameIDDeathmatch || pServerInfo->m_GameType == eGameIDTeamDeathmatch)
        pServerInfo->FragLimit = GetInt(pServer, G_FRAG_LIMIT_KEY);
    pServerInfo->TimeLimit = GetFloat(pServer, G_TIME_LIMIT_KEY);
    pServerInfo->DamageBlockTime = GetFloat(pServer, G_DAMAGE_BLOCK_TIME_KEY);
    if (pServerInfo->DamageBlockTime)
        pServerInfo->DamageBlockIndicators = GetBool(pServer, G_DAMAGE_BLOCK_INDICATOR_KEY);
    pServerInfo->AnomaliesEnabled = GetBool(pServer, G_ANOMALIES_ENABLED_KEY);
	if (pServerInfo->AnomaliesEnabled)
        pServerInfo->AnomaliesTime = GetFloat(pServer, G_ANOMALIES_TIME_KEY);
    pServerInfo->ForceRespawn = GetFloat(pServer, G_FORCE_RESPAWN_KEY);
    pServerInfo->WarmUp = GetFloat(pServer, G_WARM_UP_TIME_KEY);
	if (   pServerInfo->m_GameType == eGameIDTeamDeathmatch
		|| pServerInfo->m_GameType == eGameIDArtefactHunt
		|| pServerInfo->m_GameType == eGameIDCaptureTheArtefact)
	{
        pServerInfo->AutoTeamBalance = GetBool(pServer, G_AUTO_TEAM_BALANCE_KEY);
        pServerInfo->AutoTeamSwap = GetBool(pServer, G_AUTO_TEAM_SWAP_KEY);
        pServerInfo->FriendlyIndicators = GetBool(pServer, G_FRIENDLY_INDICATORS_KEY);
        pServerInfo->FriendlyNames = GetBool(pServer, G_FRIENDLY_NAMES_KEY);
        pServerInfo->FriendlyFire = GetFloat(pServer, G_FRIENDLY_FIRE_KEY);
	};

	if (pServerInfo->m_GameType == eGameIDArtefactHunt ||
        pServerInfo->m_GameType == eGameIDCaptureTheArtefact)
	{
        pServerInfo->ArtefactCount = GetInt(pServer, G_ARTEFACTS_COUNT_KEY);
        pServerInfo->ArtefactStayTime = GetFloat(pServer, G_ARTEFACT_STAY_TIME_KEY);
        pServerInfo->ArtefactRespawnTime = GetFloat(pServer, G_ARTEFACT_RESPAWN_TIME_KEY);
        int reinf = atoi(GetString(pServer, G_REINFORCEMENT_KEY, "0"));
		switch (reinf)
		{
		case -1:
            pServerInfo->Reinforcement = -1;
			break;
		case 0:
            pServerInfo->Reinforcement = 0;
			break;
		default:
            pServerInfo->Reinforcement = GetFloat(pServer, G_REINFORCEMENT_KEY);
			break;
		}
        pServerInfo->ShieldedBases = GetBool(pServer, G_SHIELDED_BASES_KEY);
        pServerInfo->ReturnPlayers = GetBool(pServer, G_RETURN_PLAYERS_KEY);
        pServerInfo->BearerCantSprint = GetBool(pServer, G_BEARER_CANT_SPRINT_KEY);
	}
	//--------- Read Players Info -------------------------//
	for (int i=0; i<pServerInfo->m_ServerNumPlayers; i++)
	{
		PlayerInfo PInfo;
		_snprintf_s(PInfo.Name, sizeof(PInfo.Name)-1, "%s", xrGS_SBServerGetPlayerStringValueA(pServer, i,	"player", "Unknown"));
		PInfo.Name[sizeof(PInfo.Name)-1] = 0;
		PInfo.Frags =		s16(xrGS_SBServerGetPlayerIntValueA(pServer, i,			"score", 0));
		PInfo.Deaths =		u16(xrGS_SBServerGetPlayerIntValueA(pServer, i,			"deaths", 0));
		PInfo.Rank =		u8(xrGS_SBServerGetPlayerIntValueA(pServer, i,			"skill", 0));
		PInfo.Team =		u8(xrGS_SBServerGetPlayerIntValueA(pServer, i,			"team", 0));
		PInfo.Spectator =	(xrGS_SBServerGetPlayerIntValueA(pServer, i,				"spectator", 1)) != 0;
		PInfo.Artefacts =	u8(xrGS_SBServerGetPlayerIntValueA(pServer, i,			"artefacts", 0));

		pServerInfo->m_aPlayers.push_back(PInfo);
	};
	//----------- Read Team Info ---------------------------//
	if (   pServerInfo->m_GameType == eGameIDTeamDeathmatch
		|| pServerInfo->m_GameType == eGameIDArtefactHunt
		|| pServerInfo->m_GameType == eGameIDCaptureTheArtefact
		)
	{
		for (int i=0; i<pServerInfo->m_ServerNumTeams; i++)
		{
			TeamInfo TI;
			TI.Score = u8(xrGS_SBServerGetTeamIntValueA(pServer, i, "t_score", 0));
			pServerInfo->m_aTeams.push_back(TI);
		}		
	}
};

void			CGameSpy_Browser::RefreshQuick(int Index)
{
	void* pServer = xrGS_ServerBrowserGetServer(m_pGSBrowser, Index);
	if (!pServer) return;
	ServerInfo xServerInfo;
	ReadServerInfo(&xServerInfo, pServer);
	xrGS_ServerBrowserAuxUpdateServer(m_pGSBrowser, pServer, SBFalse, SBTrue);
};

bool			CGameSpy_Browser::CheckDirectConnection(int Index)
{
	void* pServer = xrGS_ServerBrowserGetServer(m_pGSBrowser, Index);
	if (!pServer) return false;
	SBBool res = xrGS_SBServerDirectConnect(pServer);
	return res == SBTrue;
};

void			CGameSpy_Browser::OnUpdateFailed		(void* server)
{
	xrGS_ServerBrowserRemoveServer(m_pGSBrowser, server);
}

GSUpdateStatus CGameSpy_Browser::Update()
{
    xrGS_ServerBrowserThink(m_pGSBrowser);
    if (m_bTryingToConnectToMasterServer)
        return GSUpdateStatus::ConnectingToMaster;
	if (m_bShowCMSErr)
	{
		m_bShowCMSErr = false;
        return GSUpdateStatus::MasterUnreachable;
	}
    return GSUpdateStatus::Success;
};

void			CGameSpy_Browser::UpdateServerList()
{
//	SortBrowserByPing();
	if (m_pServerList) 
	{
//		m_pServerList->SetSortFunc("", false);
//		m_pServerList->SetSortFunc("ping", false);
		m_pServerList->RefreshList();
	}
}

void			CGameSpy_Browser::SortBrowserByPing	()
{
	xrGS_ServerBrowserSortA(m_pGSBrowser, SBTrue, "ping", sbcm_int);
}

bool			CGameSpy_Browser::HasAllKeys(int Index)
{
	void* pServer = xrGS_ServerBrowserGetServer(m_pGSBrowser, Index);
	if (!pServer) return true;
	ServerInfo xServerInfo;
	ReadServerInfo(&xServerInfo, pServer);
//	xrGS_ServerBrowserAuxUpdateServer(m_pGSBrowser, pServer, SBFalse, SBTrue);
	return (xrGS_SBServerHasFullKeys(pServer) == SBTrue);
};
