#pragma once

#define sPlayerBotMgr MaNGOS::Singleton<PlayerBotMgr>::Instance()

class PlayerBotEntry;
class PlayerBotAI;
struct PlayerBotStats;

enum PlayerBotState
{
	BOT_STATE_OFFLINE,
	BOT_STATE_LOADING,
	BOT_STATE_ONLINE
};

struct PlayerBotStats
{
	PlayerBotStats() : onlineCount(0), loadingCount(0), totalBots(0) {}

	// Constructor Variables
	uint32 onlineCount;
	uint32 loadingCount;
	uint32 totalBots;
	// ~Constructor Variables
};

class PlayerBotEntry
{
public:
	PlayerBotEntry(const uint64 guid, const uint32 account) : botGUID(guid), accountID(account), state(BOT_STATE_OFFLINE), requestRemoval(false), ai(nullptr) {}

private:
	// Constructor Variables
	uint64 botGUID;
	uint32 accountID;
	PlayerBotState state;
	bool requestRemoval;
	PlayerBotAI* ai;
	std::string botName;
	std::string accountName;
	std::vector<WorldPacket> m_pendingPackets;
	// ~Constructor Variables

public:
	[[nodiscard]] uint64 GetGUID() const { return botGUID; }
	[[nodiscard]] std::string GetName() const { return botName; }
	[[nodiscard]] std::string GetAccountName() const { return accountName; }
	[[nodiscard]] uint32 GetAccountID() const { return accountID; }
	[[nodiscard]] uint8 GetState() const { return state; }
	[[nodiscard]] bool GetRequestRemoval() const { return requestRemoval; }
	[[nodiscard]] std::vector<WorldPacket> GetMailBox() const { return m_pendingPackets; }
	[[nodiscard]] PlayerBotAI* GetAI() const { return ai; }

	void SetGUID(const uint64 _playerGUID) { botGUID = _playerGUID; }
	void SetName(const std::string& _botName) { botName = _botName; }
	void SetAccountName(const std::string& _accountName) { accountName = _accountName; }
	void SetAccountID(const uint32 _accountID) { accountID = _accountID; }
	void SetState(const PlayerBotState _state) { state = _state; }
	void SetRequestRemoval(const bool _requestRemoval) { requestRemoval = _requestRemoval; }
	void SetMailBox(const WorldPacket& _packet) { m_pendingPackets.push_back(_packet); }
	void ClearMailBox() { m_pendingPackets.clear(); }
	void SetAI(PlayerBotAI* _ai) { ai = _ai; }
};

class PlayerBotMgr
{
public:
	PlayerBotMgr() : m_maxAccountId(0) {}

private:
	// Constructor Variables
	uint32 m_maxAccountId;
	// ~Constructor Variables

	void DeleteAll();
	void SetBotOffline(PlayerBotEntry* e);
	void SetBotOnline(PlayerBotEntry* e);
	void UnregisterBot(const std::map<uint64, PlayerBotEntry*>::iterator& iter);
	uint32 GenBotAccountId() { return ++m_maxAccountId; }

	ShortTimeTracker Manager_Update_Timer{ 10 * IN_MILLISECONDS };
	std::map<uint64, PlayerBotEntry*> m_bots;
	PlayerBotStats m_stats;

public:
	[[nodiscard]] PlayerBotEntry* GetBotWithName(const std::string& name) const;
	[[nodiscard]] PlayerBotStats GetBotStats() const { return m_stats; }
	static void OnPlayerInWorld(Player* pPlayer);
	bool AddBot(PlayerBotAI* ai);
	void Load();
	void Update(uint32 diff);
	void LoadTalentMap();
	void LoadSpellTrainers();
	void LoadSpellMap();
	void LoadNameMap();

	// Bot Update Variables
	bool BOT_UPDATE_INTERVAL_MANUAL = false;
	uint32 BOT_UPDATE_INTERVAL = 1000;
	uint32 BOT_UPDATE_INTERVAL_PREVIOUS = 0;
	ShortTimeTracker BOT_UPDATE_INTERVAL_TIMER{ 60 * IN_MILLISECONDS };
	// ~Bot Update Variables

private:
	// Loader Variables
	typedef std::unordered_map<std::string, uint32> TalentID;
	std::unordered_map<Classes, TalentID> TalentMap;
	std::unordered_map<Classes, uint32> SpellTrainers;
	std::unordered_map<std::string, std::set<uint32>> spellNameEntryMap;
	std::map<std::pair<Races, Gender>, std::vector<std::string>> NameMap;
	std::vector<std::string> NamesSuffixes;
	// ~Loader Variables

public:
	[[nodiscard]] std::unordered_map<Classes, TalentID> GetTalentMap() const { return TalentMap; }
	[[nodiscard]] std::unordered_map<Classes, uint32> GetSpellTrainers() const { return SpellTrainers; }
	[[nodiscard]] std::unordered_map<std::string, std::set<uint32>> GetSpellMap() const { return spellNameEntryMap; }
	[[nodiscard]] std::map<std::pair<Races, Gender>, std::vector<std::string>> GetNameMap() const { return NameMap; }
	[[nodiscard]] std::vector<std::string> GetNameSuffixes() const { return NamesSuffixes; }
};