#include <stdio.h>
#include "antidll.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include <fstream>

AntiDLL g_AntiDLL;
PLUGIN_EXPOSE(AntiDLL, g_AntiDLL);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

IUtilsApi* g_pUtils;
IPlayersApi* g_pPlayers;

std::vector<std::string> g_vEvents;

int g_iPunishType;
const char* g_szPunishCommand;
const char* g_szChatMessage;
bool g_bLogs;
float g_flInterval;

bool g_bPunished[64];

CGameEntitySystem* GameEntitySystem()
{
	return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
}

bool AntiDLL::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	return true;
}

bool AntiDLL::Unload(char *error, size_t maxlen)
{
	ConVar_Unregister();
	
	return true;
}

void LoadConfig()
{
	KeyValues* pKVConfig = new KeyValues("Config");
	if (!pKVConfig->LoadFromFile(g_pFullFileSystem, "addons/configs/AntiDLL/settings.ini")) {
		g_pUtils->ErrorLog("[%s] Failed to load config addons/configs/AntiDLL/settings.ini", g_PLAPI->GetLogTag());
		return;
	}

	g_iPunishType = pKVConfig->GetInt("punish_type", 0);
	g_szPunishCommand = pKVConfig->GetString("punish_command", "");
	g_szChatMessage = pKVConfig->GetString("chat_message", "");
	g_bLogs = pKVConfig->GetBool("logs", false);
	g_flInterval = pKVConfig->GetFloat("interval", 5.0f);
}

void LoadEventFile()
{
    char szPath[512];
    g_SMAPI->Format(szPath, sizeof(szPath), "%s/addons/configs/AntiDLL/events.txt", g_SMAPI->GetBaseDir());
    std::ifstream file(szPath);
    if (!file.is_open())
    {
        g_pUtils->ErrorLog("[%s] Failed to load events file", g_PLAPI->GetLogTag());
        return;
    }
    std::string line;
    while (std::getline(file, line))
    {
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        g_vEvents.push_back(line);
    }
    file.close();
}

void ReplaceString(std::string& str, const std::string& from, const std::string& to)
{
	size_t start_pos = 0;
	while((start_pos = str.find(from, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
}

void OnClientAuth(int iSlot, uint64 iSteamID64)
{
	g_bPunished[iSlot] = false;
}

void AntiDLL::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pPlayers = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Players system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}

	LoadEventFile();
	LoadConfig();
	g_pPlayers->HookOnClientAuthorized(g_PLID, OnClientAuth);
	g_pUtils->StartupServer(g_PLID, StartupServer);
	g_pUtils->CreateTimer(g_flInterval, [](){
		for (int i = 0; i < 64; i++)
		{
			if(g_pPlayers->IsFakeClient(i)) continue;
			if(g_bPunished[i]) continue;
			IGameEventListener2* pListener = g_pPlayers->GetLegacyGameEventListener(i);
			if(!pListener) continue;
			for (auto& event : g_vEvents)
			{
				if(g_pUtils->GetGameEventManager()->FindListener(pListener, event.c_str()))
				{
					g_bPunished[i] = true;
					const char* szName = g_pPlayers->GetPlayerName(i);
					char szSteamID64[64];
					g_SMAPI->Format(szSteamID64, sizeof(szSteamID64), "%llu", g_pPlayers->GetSteamID64(i));

					if(g_bLogs)
						g_pUtils->LogToFile("AntiDLL", "Player %s(%s) is using event %s\n", szName, szSteamID64, event.c_str());
					
					if(g_szChatMessage && g_szChatMessage[0])
					{
						std::string sMessage = g_szChatMessage;
						ReplaceString(sMessage, "{name}", szName);
						ReplaceString(sMessage, "{steamid}", szSteamID64);
						ReplaceString(sMessage, "{userid}", std::to_string(i));
						g_pUtils->PrintToChatAll(sMessage.c_str());
					}

					if(g_iPunishType) {
						std::string sMessage = g_szPunishCommand;
						ReplaceString(sMessage, "{name}", szName);
						ReplaceString(sMessage, "{steamid}", szSteamID64);
						ReplaceString(sMessage, "{userid}", std::to_string(i));
						engine->ServerCommand(sMessage.c_str());
					} else {
						engine->DisconnectClient(i, NETWORK_DISCONNECT_KICKED);
					}
					break;
				}
			}
		}
		return g_flInterval;
	});
}

///////////////////////////////////////
const char* AntiDLL::GetLicense()
{
	return "GPL";
}

const char* AntiDLL::GetVersion()
{
	return "1.0.1";
}

const char* AntiDLL::GetDate()
{
	return __DATE__;
}

const char *AntiDLL::GetLogTag()
{
	return "AntiDLL";
}

const char* AntiDLL::GetAuthor()
{
	return "Pisex";
}

const char* AntiDLL::GetDescription()
{
	return "AntiDLL";
}

const char* AntiDLL::GetName()
{
	return "AntiDLL";
}

const char* AntiDLL::GetURL()
{
	return "https://discord.gg/g798xERK5Y";
}
