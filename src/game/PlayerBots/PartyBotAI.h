#pragma once
#include "CombatBotBaseAI.h"
#include "PlayerBotAI.h"

class PartyBotAI final : public CombatBotBaseAI
{
public:
	PartyBotAI(Player* pLeader, Player* pClone, const CombatBotRoles role, const uint8 class_, const uint8 race, const uint8 gender, const uint32 level, const uint32 mapId, const uint32 instanceId, const float x, const float y, const float z, const float o)
		: CombatBotBaseAI(), m_leaderGUID(pLeader->GetObjectGuid()), m_race(race), m_class(class_), m_level(level), m_gender(gender), m_mapId(mapId), m_instanceId(instanceId), m_x(x), m_y(y), m_z(z), m_o(o)
	{
		m_role = role;
		if (pLeader->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
			pLeader->IsHiringCompanion = true;
		m_cloneGuid = pClone ? pClone->GetObjectGuid() : ObjectGuid();
		if (pClone) IsClone = true;
		m_updateTimer.Reset(100);
	}
	bool OnSessionLoaded(const PlayerBotEntry* entry, WorldSession* sess) override
	{
		return SpawnNewPlayer(sess, m_class, m_race, m_gender, m_mapId, m_instanceId, m_x, m_y, m_z, m_o, sObjectAccessor.FindPlayer(m_cloneGuid), m_leaderGUID);
	}

	// Constructor Variables
	ObjectGuid m_leaderGUID;
	ObjectGuid m_cloneGuid;
	ShortTimeTracker m_updateTimer;
	uint8 m_race = 0;
	uint8 m_class = 0;
	uint32 m_level = 0;
	uint8 m_gender = static_cast<uint8>(urand(0, 1));
	uint32 m_mapId = 0;
	uint32 m_instanceId = 0;
	bool IsClone = false;
	float m_x = 0.0f;
	float m_y = 0.0f;
	float m_z = 0.0f;
	float m_o = 0.0f;
	// ~Constructor Variables

	// Commands Variables
	bool IsStaying = false; // for .z stay
	bool IsPassive = false; // for .z stop
	bool IsComeToggle = false; // for .z cometoggle
	// ~Commands Variables
	
	// OnWhisper Variables

	// ~OnWhisper Variables

	// General Variables

	// ~General Variables

	// Update Functions
	void UpdateInCombatAI();
	void UpdateInCombat_Warrior();
	void UpdateInCombat_Paladin();
	void UpdateInCombat_Hunter();
	void UpdateInCombat_Rogue();
	void UpdateInCombat_Priest();
	void UpdateInCombat_Shaman();
	void UpdateInCombat_Mage();
	void UpdateInCombat_Warlock();
	void UpdateInCombat_Druid();

	void UpdateOutOfCombatAI();
	void UpdateOutOfCombat_Warrior();
	void UpdateOutOfCombat_Paladin();
	void UpdateOutOfCombat_Hunter();
	void UpdateOutOfCombat_Rogue();
	void UpdateOutOfCombat_Priest();
	void UpdateOutOfCombat_Shaman();
	void UpdateOutOfCombat_Mage();
	void UpdateOutOfCombat_Warlock();
	void UpdateOutOfCombat_Druid();

	void UpdateAI(uint32 diff) override;
	// ~Update Functions
};
