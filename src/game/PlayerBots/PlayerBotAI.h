#pragma once
#include "PlayerAI.h"

class PlayerBotEntry;

class PlayerBotAI : public PlayerAI_NYCTERMOON
{
public:
	explicit PlayerBotAI(Player* pPlayer = nullptr) : PlayerAI_NYCTERMOON(pPlayer), botEntry(nullptr) {}

	virtual bool OnSessionLoaded(const PlayerBotEntry* entry, WorldSession* sess);
	void GenerateCompanionName(std::string& Name, ObjectGuid LeaderGUID, const Races Race, const Gender Gender) const;
	virtual void OnPlayerLogin() {}
	virtual void OnPacketReceived(WorldPacket const& /*packet*/) {} // server has sent a packet to this session
	virtual void ProcessPacket(WorldPacket const& /*packet*/) {} // ai has scheduled delayed response to packet
	bool SpawnNewPlayer(WorldSession* sess, uint8 _class, uint8 _race, uint8 gender, uint32 mapId, uint32 instanceId, float x, float y, float z, float o, Player* pClone = nullptr, ObjectGuid m_leaderGuid = ObjectGuid()) const;

private:
	PlayerBotEntry* botEntry;

public:
	[[nodiscard]] PlayerBotEntry* GetBotEntry() const { return botEntry; }
	void SetBotEntry(PlayerBotEntry* _botEntry) { botEntry = _botEntry; }
};
